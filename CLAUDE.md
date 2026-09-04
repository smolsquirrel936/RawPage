# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository currently is

**Stage 0 scaffolding plus all fifteen tasks of Stage 1.** The product is **RawPage**, a
Windows-first desktop PDF editor modeled on Right PDF Professional. The design lives under
`claude-plan/`.

### Where the work is right now — read this first

**Stage 1 is COMPLETE and signed off. Stage 2 is under way: T1–T13 of 18 are done** — the engine
boundary (`core/backend.h`), PDFium made permanent behind `pdf/render/PdfiumInit`, **risk A2
measured: `go`** (all three on 2026-09-01), **the corpus: 60 generated files** and **`pdf/cos/Lexer`
— the first token of PDF this project has ever read for itself** (both 2026-09-02),
**`pdf/cos/CosObject` + `Parser` — the first PDF OBJECT** (2026-09-03), and **the nine filters,
`XrefTable`, then `XrefRepair` with `RepairReport`, `Revision` and `Scope`** (2026-09-04), which
**meets the Stage 2 exit criterion on Windows**: every one of the 24 `corpus/malformed/` files
yields a report and never a crash (its ASan half, **A-24**, is a Linux job and is unrun). And
**T9 on the same day: `rawpage.exe` OPENS A PDF AND DRAWS A PAGE** — `PdfiumDocument`,
`PdfiumRasterizer`, the real `DocumentBackendImpl`, and `core/doc/Page` + a read-only `PageTree`.
**And T10 and T11 on the same day: `core/render/` is COMPLETE** — `DpiPolicy`, `TileGrid` and
`PagePresentation` (T10), then `RenderCache` with its two tiers and `RenderScheduler` (T11), which
**closes I-05** by choosing the arrangement D2-9 left open: **one render thread with a queue**.
**And T12 and T13 on the same day: THE PRODUCT DRAWS A PAGE ON SCREEN.** T12 shipped
`WinDisplayTopology` and `WinPowerSession` — taking the Windows bundle from two of seventeen
interfaces to **four**, and giving `DpiPolicy` its third input for real: the P-1 rig's 4K panel
measures **163 DPI of glass at 150 %**, where the nominal `96 * scale` every caller had been passing
says 144. T13 shipped **`ui/viewer/`** — `ViewportController`, `PageLayoutEngine`, `TilePresenter` and
`PageView` — and **`DocumentWindow` stopped being D1-2's stub**. `PageView` is **the first widget in
the product to declare a `KeyContext`** (D2-8), so `ShortcutBinder::activeContext()` answers something
other than `Global` for the first time in the product's history.
**Next action: Stage 2 T14** — the §8 Viewer commands, the status bar's real page numbers, `MessageBar`
with the repair report and the password prompt, and **I-53's `tooltipKey`** (D2-10). Everything T13 put
on screen is currently reachable only by scrollbar, wheel and `Ctrl`+wheel: **not one Viewer chord is
registered**, and the status bar still shows Stage 1's em dash. Live status, task by task and
deviation by deviation, lives in the **Stage 1 and Stage 2 status blocks** in
`claude-plan/Implementation-Plan.md` — those blocks are the authority, and this section is their
summary. The table below is Stage 1's, updated where Stage 2 has already moved a number.

| | |
|---|---|
| **Tests** | **598/598** on `x64-debug` and `x64-release` as of Stage 2 T13, with **one explicitly SKIPPED** — the battery case, on a desktop that has none, which is guide case A-62's own *"report not applicable explicitly rather than pass vacuously"* requirement honoured rather than a gap (66 at the end of Stage 0; 334 at Stage 1 T15, +5 from the M-03 fixes, +2 from the M-04 fixes, +2 from M-05's, +4 from M-06's, +6 from M-11's I-47, I-49, I-50, I-52 and the toolbar lock, +1 from M-12's I-54, giving Stage 1's final 354; then +13 at Stage 2 T1 — 6 `unit.` backend cases, 5 `gates.headless_assert.*` and 2 `gates.layer_lint.*`; +16 at T2 — 4 `render.`, 5 `gates.layer_lint.fpdf*` and 7 `gates.pdfium_pin.*`; +1 at T3, the A2 probe; +3 at T4, the `gates.corpus.*` trio; +25 at T5; +31 at T6; +29 at T7 — 13 `unit.cos filters`, 14 `unit.cos xref` and 2 `gates.license_audit.undeclared_transitive_*`; +20 at T8 — 12 `unit.cos repair` and 8 `unit.doc`; **+34 at T9** — 22 `render.` over the document and the rasterizer, 11 `unit.` over `Page`/`PageTree`/`RenderRequest`, and one `ui.` for A-47); **+18 at T10** — 4 `unit.` over `DpiPolicy`, 7 over `TileGrid` and 7 over `PagePresentation`, every one of them headless; **+17 at T11** — 6 `unit.` over `RenderCache` and its two tiers, 11 over `RenderScheduler`; **+9 at T12** — 5 `platform.` over `WinDisplayTopology` and 4 over `WinPowerSession`, one of which is the skip; and **+28 at T13** — 13 `ui.` over `ViewportController` and `PageLayoutEngine` with no window at all, 10 `ui.` over `PageView` and `TilePresenter`, 2 `ui.` for `KeyContext::Viewer` and its negative, 1 net in the tab suite where A-57's stub case became two, and 2 `render.` for the real engine on the real scheduler); the bare `gates` preset is **90/90** with no Qt. Plus **two benchmarks**, `bench.ribbon.tab_switch` and — since S2 · T3 — `bench.render.a2_probe`; this line read *"one benchmark"* until 2026-09-03, having missed the second for the same reason the target-count heading below did. A bench reports a figure and its exit code is about whether the measurement was *possible*, never whether the machine was fast |
| **Gates** | `layer-lint` clean at **247 files / 1,622 includes**, **8** live filesystem-write waivers — the two new ones both in `core/render/DiskTileStore.cpp`, one write and one delete, because every deletion in that file is funnelled through a single helper to keep the census at two; `license-audit` clean at **20 components** — 17 until T7 taught it to enumerate the RESOLVED CLOSURE rather than the declared manifest (I-02); `i18n-lint` clean at **163 files / 2 catalogs, 0 waivers** (it scans `pdf/` and `platform/` from T2 — see I-04). **Three gates live**, not four — see the gate table below. `tools/ribbon-validate` is a build step, not a gate |
| **Verdicts recorded** | **A3 = `go, with named custom work`**. T15 **re-measured R-9 against the shipped ribbon** rather than against T2's throwaway spike — p95 **0.471 ms** against a 16.667 ms budget, over 192 real controls, with a control proving the instrument was live. Both figures stand in `Tech-Stack-Plan.md` §3.3; the second is the one that is a claim about the product. **`-municode` = not adopted**; A5, A6, A7 from Stage 0 |
| **Issue log** | I-01…I-55 in `Stage1-AppShell-Testing-Guide.md` §8. **None open.** **I-54 (S2)**: the tool finder took only the **first character** of a query **and lost its blinking caret** — one report, two mechanisms, because opening a `Qt::Popup` takes both the keyboard and the window activation away from the widget behind it. Keys: a popup is a WINDOW, so once it opened Qt delivered every further keystroke to it rather than to the search box, and a `QListWidget` spent them on its own keyboard search; Backspace went the same way, so a mistyped query could not be corrected. Caret: a `QLineEdit` in a **deactivated** window stops painting its cursor whether or not it still holds focus. **Fixed in two rounds** — the first addressed the keys only, and the user found the caret still gone by using the repaired build, I-45's lesson for the third time. `setFocusPolicy(Qt::NoFocus)` had carried the comment *“typing stays in the box”* since T11 and could never have made it true: focus policy picks a widget **within** a window, and this is about which **window** the keyboard talks to. `eventFilter` now keeps Up/Down/PageUp/PageDown/Enter and forwards the rest to the line edit, and the list carries `WA_ShowWithoutActivating` and a focus proxy back to the box — with `Qt::NoFocus`, **all three of the things `QCompleter` does with its own popup**, which this widget is in all but name. **The third defect this one popup has produced** (I-44, I-45), and nine green `[toolfinder]` cases missed all of it because every one types at `searchBox()` — the one address a real keystroke does not reach while the list is up: convention 19's back door, I-34's and I-43's shape. **Raised by the user after M-12 was already signed off**, which re-opened and re-closed the no-open-S2 box. **I-53** is not a defect: plan §5.1's *registered-but-disabled* tooltip was never built — an unregistered control says it is not implemented, one that is merely unavailable says nothing — and the user's call was **planned absence, Stage 2**, since 338 of 361 ids are unregistered and the few registered-and-disabled ones are refused for reasons visible on screen. **I-50 (S3)**: only the 22 *enabled* commands could be added to the toolbar — Qt sends no input event to a disabled widget, so right-clicking any of the other 338 did nothing — and the toolbar's own empty space offered an `Add` that could never be enabled. `Ribbon::contextMenuEvent` now resolves the position with `childAt()`, which does return disabled children, and the menu is keyed on what is under the cursor. **I-51 (S3)**: the startup line printed the DROPPED override count as the applied one, so a session that applied a remap perfectly logged `0 of 1 user overrides applied` — a wrong diagnostic that sent its reader hunting a defect that was not there. **I-52 (S3)**: a defect in the drag feature two hours old — a slot among the buttons is not an index into the layout, so a drop past the last button landed on the stretch and the button jumped to the far right edge. **I-49 (S2)**: **every** Quick Access Toolbar customization made the toolbar vanish — a widget built with a parent that is already on screen starts hidden, and a hidden widget is an empty layout item, so add/remove/reorder/reset each produced invisible buttons and a 4-pixel strip. `ui.resetCustomization` rebuilt it invisibly too, so restarting was the only way back. Nine `[qat]` cases missed it because every one asks the command LIST, which stays correct for a row nobody can see — the sixth time this stage has been caught by that (I-34, I-42, I-43, I-46, I-47). **I-47 (S2)**: every preference lived only in memory until a **clean** shutdown — `Settings::sync()` was called from one place, `CompositionRoot`'s teardown — so a crash or a kill lost settings the user had watched take effect, which the no-OK-button Preferences dialog makes worse rather than better. Now flushed on every change, with teardown as a backstop. Missed by the suite because every settings case called `sync()` itself: convention 19's back door again. **I-48** is not a defect: two of M-11's steps tested things that do not exist — a recent-files surface (declared, unregistered, no UI: the **fifth** built-but-unreachable capability after I-34, I-42, I-43, I-46) and a window-geometry restore, which is `SessionState` at Stage 4. **I-46** is not a defect but is worth knowing: **nothing in the product can register the PDF file association** — all four calls are implemented and tested, and the only callers are the tests, which is this stage's recurring shape for the fourth time (I-34, I-42, I-43). The user's call was **planned absence, Stage 14/15**; M-07 walks against a hand-made Windows association, and its *Pre* now also names the `windeployqt` step it had always needed. **I-45 (S2)** is the one to remember: it was a **regression in I-44's own fix** — dismissing the list on any `FocusOut` closed it in the same turn it opened, because showing a `Qt::Popup` *is* a focus change, so no search showed anything. Found by the user re-walking the repaired build within the hour, and invisible to the case added with I-44 because offscreen that focus change does not happen. **I-44 (S2, and the worst defect this stage has produced)**: a tool-finder query that matched nothing **froze the application** — the result list is a `Qt::Popup` window holding the mouse grab, the *"nothing matched"* message is itself a row so the list still showed, and `Esc`, an outside click and `Enter` all failed to put it away. Force-close was the only exit. Three exits added, each independently missing, plus `FocusOut`/`WindowDeactivate`. **I-43 (S3)**: reordering the Quick Access Toolbar could not be done by a person — `moveCommand` public since T11, the order persisted since T11, **three documents calling it shipped**, and no menu item, no drag and no `ActionId` to reach it; the only caller was a test that called the method. I-34's shape, and the third instance this stage. Fixed with `ui.qatMoveLeft` / `ui.qatMoveRight`, each disabled at the end it cannot move towards, and the case asserted the **route** (the real context menu offers them) as well as the dispatch, with a control for each. **Superseded hours later in the same walk**: the user asked for drag-to-reorder, so `buildMenu` dropped both items and `ui.qatLock` joined them — the two commands stay **registered and unlisted** (the tool finder and flow K still reach them), and the case's route half was *inverted rather than deleted*, now asserting they are registered **and absent from the menu**. Those three ids are **the only ones Stage 1 has added to the plan's 358**, giving 361 declared and 23 registered. **I-42 (S2)**: pressing `Alt` did nothing in any locale — the ribbon has *drawn* key tips since T10 and nothing ever asked it to, because `AppRibbonKeyTips` was bound to `Alt` and never registered. **R-5 had been recorded as met against the T2 spike**, whose handler the shipped ribbon never inherited — I-36's shape again. Registered (the **twentieth** id), and the tip letters moved out of the tab titles into a `ribbon.keytip.*` catalog family, because the first-character scheme gave `zh-TW` 常 建 編 註 組 表 保 檢 — untypable, with 表 claimed twice. **I-41 (S2)**: Qt's native `windows11` style paints control text from the colour scheme **it** derives from Windows and ignores the application palette, so the dark theme was in force for every `QLabel` and for **no** `QToolButton` — the ribbon, the toolbar and the tool finder. Measured on a light OS with the dark palette installed: an enabled button's glyph was `#1f1f1f` on a `#1f1f1f` background, a disabled one `#181b1f`. `apply()` now calls `QStyleHints::setColorScheme()` as well, `Unknown` for follow-system so the OS keeps the decision. Found only because the I-40 fix below did not work — the token table was the one place anyone had looked. **I-40 (S3)**: in dark mode the shell's **disabled** text could not be read — and disabled text is most of this shell, since 358 commands are declared and 19 registered. Measured 3.05 dark and **2.54 light**, so the light theme two manual passes had already signed off was the worse of the two; both raised (4.77 / 3.87 against the window), with a new case measuring every text token against the surface it is painted on, in both themes, and asserting disabled stays under half the enabled ratio so it still reads as disabled. **Not an accessibility criterion** — that left the product on 2026-08-23 — but whether the ribbon can be read. **I-39 (S3)**: the auto-hide edge strip stayed on screen beside the pane it had just summoned — true since T9, and invisible until I-37 made a peek something a person could perform, because every earlier case reached `peek()` in a fixture where visibility was not the subject. **I-37 (S3)**: an auto-hidden sidebar could be revealed only by *clicking* the edge strip, never by reaching for it — closed by the user's decision to move the product to the guide rather than the guide to the product, so the strip now peeks on `QEvent::Enter` and still answers a click. **I-38** is not a defect: `general/touchMode` is stored and read by nothing, and the user's call was **planned absence, Stage 14** — it is now a row in the guide's §1 and in `Stage1-AppShell-Plan.md` §11 rather than an open issue, and M-04's steps no longer ask for it. **I-36 (S2)**: opening M-04 found that **no ribbon tab was ever drawn** — `QSizePolicy::Ignored` tells QBoxLayout the tab bar's width preference is not a number, and next to a separate `addStretch(1)` the spacer took the whole strip, so the `QTabBar` was laid out **0 px wide at every window size** from T10 onward. Eleven green ribbon cases missed it because each asked the bar what it *contained* and none asked what the **layout gave it**; fixed, with a case and its control. **I-33, I-34 and I-35 were raised by the user's M-03 walk on 2026-08-30 and are Stage 1's first findings in the PRODUCT rather than in the harness** — every earlier one was a test, a gate, a document or the CI. **I-34 (S2)**: re-docking a tab into another window could not be performed by a person, because `DocumentTabBar` reported the gesture at the instant the 40 px threshold was crossed rather than at the release, giving the drag one sample to land on a ~25 px strip; the branch it fed was correct and had never been driven by test or by hand. **I-33 (S3)**: tearing a window's ONLY tab out left the original with no tabs, because `detachDocument` guarded the index but never the count. **I-35 (S3)**: a window emptied by re-docking its last tab away stayed open — reachable only *after* I-34 was fixed, which is why a repaired case is re-walked rather than signed off on the strength of the repair. All fixed the same day — five cases, five negative controls, 339/339. I-04 closed at T8; I-12…I-16 at T8/T9; **I-17** by the first `portable-asan` run to reach Stage 1's code; **I-18…I-20** by T10's first run against a real, shown window; **I-21** and **I-22** at T11 and **I-23** at T12 — all three by running something new against the product; **I-24** and **I-25** at T13, and both of those were the *harness* rather than the product — a case that asserted the opposite of what it meant, and a gate that flagged a diagnostic string; **I-26…I-28** at T14 — a remap that *added* a chord instead of replacing one (found by the first caller that ever remapped for a user), a gate about to report forty notes nobody could act on, and a log file that could not name the binary that wrote it; and **I-29 and I-30** at T15, again the harness and its infrastructure rather than the product — a benchmark whose every printed figure was its own argument-evaluation bug, and (the S2) **a CI job that had never run because two systems had to agree on a variable name and nothing checked that they did**. *(Stage 1 now has its own I-15 and I-16; prose citing “I-15's lesson” or “I-20” means **Stage 0's** — the guide's §8 header says so.)* |
| **Manual pass** | **M-01 ✅ signed off 2026-08-23; M-02 ✅** — signed off 2026-08-23 for the ribbon, then **re-walked in full on 2026-08-30** against the shipped shell: all four steps including the mid-session scale change and the re-login, **no findings**, which retired R-6's last scoping note and satisfied precondition P-1 outright. **M-03 ✅ walked and signed off 2026-08-30**, in full — and it found **I-33, I-34 and I-35**, the stage's first three defects in the product, all fixed and the case re-walked before sign-off. **M-03 became walkable at T12 and M-12 at T13. T14 made M-11 fully walkable and unblocked M-02's non-ribbon half** — its steps name the sidebar, a floating pane *and Preferences*, and T14 made all three real, which is what let that re-walk happen as written. **M-04 ✅ walked and signed off 2026-08-30**, after **three passes and four findings**, each repair making the next defect reachable: it stopped at step 1 because no View tab existed to click (**I-36**); the second pass, on the repaired build, produced **I-37** and **I-38**; fixing I-37 created a gesture that had never existed, and walking *that* found **I-39**. None was a regression — two had shipped since T9 and T10, green in CI throughout. **M-05 ✅ walked and signed off 2026-08-31** — mis-listed as unwalkable until 2026-08-30, then it found **I-40** and **I-41**, the second only because the first fix changed nothing a person could see. **M-06 ✅ walked and signed off 2026-08-31**, the longest walk of the stage: every surface in `zh-TW`, the editorial review of 29 unfinished entries, and four defects — **I-42** (`Alt` raised no key tips in any locale), **I-44** (a query matching nothing froze the application), **I-45** (the fix for it closed the list in the same turn it opened) and **I-43**, which surfaced here and belongs to M-11. **M-07 ✅ walked and signed off 2026-08-31** — flow B end to end by hand, all five steps, one process throughout, both awkward filenames correct. **M-11 ✅ signed off 2026-08-31**, the case that found the most: **I-47**, **I-49**, **I-50**, **I-51**, **I-52** — the last a defect in a fix made hours earlier during the same walk — plus the drag-to-reorder and lock feature the user asked for mid-case. **M-12 ✅ walked and signed off 2026-08-31 — the last case of the stage.** No S1: nothing claimed to have saved, printed or undone anything, the stub tab did not pretend to be a document, and **no chord was intercepted by the window manager, the IME or the platform before Qt saw it**, which is the one thing in that case no synthetic key event could have established. Its one finding, **I-53**, was disposed of as a planned absence. **All nine walkable cases are now ✅** — M-01 … M-07, M-11 and M-12; M-08 was withdrawn with accessibility and M-09 and M-10 are not walkable |

**STAGE 1 IS COMPLETE. Every Definition-of-Done item is closed as of 2026-08-31**, the manual walk
included — it was the last one, and it was never code. M-01 … M-04 were signed off on 2026-08-30,
**M-05, M-06, M-07, M-11 and finally M-12 on 2026-08-31**. M-08 was withdrawn with accessibility;
M-09 and M-10 are not walkable, their subject being code no stage before Stage 2 writes. **I do not
mark a manual case** — every one of these was walked and signed off by the user.

### Stage 2 — where it is, and what its next task inherits

**Next action: Stage 2 T14** — the §8 Viewer commands, `StatusBar`'s real page numbers and zoom,
`ui/shell/MessageBar` (which does not exist) carrying the repair report and the password prompt, and
**I-53's `tooltipKey`** (D2-10). It inherits a page on screen and **not one command that acts on it**:
every §8 Viewer chord resolves in the right context and then finds nothing registered, which is
`NotFound` and deliberately leaves the key unconsumed. Two things T14 should know before it starts.
**The route exists and is proven** — `PageView` declares `KeyContext::Viewer`, guide case A-56 shows a
`Viewer`-bound chord dispatching in the viewer and *not* from the tool finder's search box, and the
case had to register `ToolHand` itself to show it, saying so at the assertion. And **`repairReport()`
is sitting in exactly the position `pages()` was in before T13** — declared on the concrete
`pdf::DocumentBackendImpl`, where `ui/` cannot reach it (issue **I-08**); the message bar is the
consumer that will find that out, and moving it onto `core::IDocumentBackend` is a two-line change
made deliberately at its first caller rather than speculatively now.
Stage 2 is where documents start being opened, parsed and drawn — and where **I-53** stops being
theoretical (see below). **Stage 2's `T1`…`T18` are a different namespace from Stage 0's and
Stage 1's `T1`…`T15`** — always say which stage.

**T12 shipped the two OS interfaces the policy layer was waiting on (2026-09-04).** The Windows
bundle goes from **two of seventeen** interfaces to **four**. Four things bind what comes next:

- **`DpiPolicy`'s third input is now real, and the difference is measurable rather than notional.**
  Measured through the product's own query on the P-1 rig: `\\.\DISPLAY1` is **3840×2160 at 150 %
  with 163 DPI of glass**, `\\.\DISPLAY2` is **2560×1440 at 100 % with 108**. The nominal
  `96 * scale` says 144 and 96 — a 13 % error on the 4K panel, which is exactly the *"renders, looks
  nearly right, and is wrong on somebody else's monitor"* failure `DpiPolicy`'s header warns about.
  The effective DPI and the raw DPI are reported **separately** and neither is derived from the other.
- **A DPI-UNAWARE PROCESS IS HANDED FIGURES THAT ARE SELF-CONSISTENT AND WRONG**, and this is now
  checkable rather than assumed. `WinDisplayTopology::processIsPerMonitorAware()` exists for it,
  guide case A-60 `REQUIRE`s it before asserting anything, and `tests/platform/main.cpp` sets
  per-monitor-v2 awareness before `QCoreApplication`. Without that line Windows would have reported
  the 4K panel as 2560×1440 at 100 % and the whole suite would have agreed with itself. That is also
  **I-07**: `platform/platform.h` documented `MonitorInfo`'s geometry as *device-independent* pixels,
  which is the virtualized answer — the comment described the failure mode as the specification.
- **`changed` fires on a real DIFFERENCE, not on a message**, and `subscribe()` has no product caller
  yet **on purpose**. One scale change in the Settings app produces several notifications and
  `WM_SETTINGCHANGE` is broadcast for things that are not displays, so every notification takes a
  fresh snapshot and compares. As for the caller: `PageView` re-reads the topology on Qt's own
  screen-change events, because `IDisplayTopology` has **no `unsubscribe`** and a per-window
  subscription would dangle when a torn-off window closes (convention 10). Its real consumer is
  `core/settings/SessionState` at Stage 4.
- **`platform/win/WinMessageWindow` is a file the plan's manifest did not list**, added with its own
  FileMap row on T11's `DiskTileStore` precedent: on Windows a notification IS a window message, both
  T12 classes need one, and `platform/` may not own a `QWidget`. It is a **top-level** window and not
  `HWND_MESSAGE`, which is the load-bearing detail — message-only windows do not receive broadcasts,
  so the obvious choice would have registered successfully and silently never fired.
  **The battery THROTTLE is T15's, not T12's**: it needs `performance/throttleOnBattery`, and
  `setActiveWorkerLimit()` clamps to `[1, workerCount]` where `workerCount` is 1, so wiring it now
  would build a path no configuration could make behave differently.

**T13 SHIPPED THE THING THE STAGE IS NAMED FOR (2026-09-04): a page is on screen.** `ui/viewer/` holds
four classes and `DocumentWindow` stops being D1-2's stub. Six things bind what comes next:

- **The split inside `ui/viewer/` is by QUESTION, and it is what makes the viewer testable at all.**
  `ViewportController` names no widget — zoom, the scroll clamp and the view history are arithmetic
  over four numbers, and all thirteen of its cases run with no window. `PageLayoutEngine` resolves
  `DpiPolicy`'s three inputs and answers the fit questions. `TilePresenter` is **the only place in the
  product where a `core::RasterImage` becomes a `QImage`** — and it passes `bytesPerLine`, never
  `width * 4`, because a page drawn over a padded buffer without its stride is sheared into diagonal
  garbage. `PageView` is the widget.
- **Two coordinate spaces meet in this layer and nowhere else.** Content space is presented DEVICE
  pixels; Qt lays widgets out in LOGICAL pixels. The conversion is the device-pixel ratio and it lives
  in `TilePresenter::paint()` and in `PageView::updateScrollBars()`. On a 100 % monitor the two are
  equal, so an implementation that confuses them is correct on the machine it was written on and
  two-thirds size on the other one — **and the suite cannot catch it here**, because the offscreen
  platform reports a ratio of 1.0. Manual case M-01 is where that assertion means something.
- **`PageView` is the first widget in the product to declare a `KeyContext`** (D2-8). Stage 1 shipped
  the whole context-resolution mechanism with nothing declaring one, so `activeContext()` answered
  `Global` for every keystroke the product had ever seen — which was correct, and which is why **a
  regression here looks exactly like the previous fifteen tasks' correct behaviour**. The declaration
  is on the view itself, so Stage 4's overlays and rulers inherit `Viewer` without saying anything.
- **`DocumentWindow` owns the handle, and closing it is a three-step protocol rather than a call**:
  cancel this document's render jobs, wait for the scheduler to go idle, drop its tiles from the cache,
  then close. A worker inside `FPDF_RenderPageBitmap` on a document about to be destroyed is a
  use-after-free that no mutex can prevent, which is why `IDocumentBackend::close()` states the
  protocol at its declaration. The middle step is not optional either: `RenderScheduler` holds the
  cache privately and has no *forget this document*, so a viewer that only cancelled would keep a
  closed 900-page file's megabytes for the life of the process. That is why `Services` gained
  `RenderCache&` as well as `RenderScheduler&`.
- **`CompositionRoot` owns the scheduler's ONE wake handler.** `setWakeHandler` REPLACES rather than
  chains — `core/Log.h`'s single sink in a second disguise (convention 18) — so a `PageView` that
  installed its own would silence every other view the moment a second tab opened. The wake is called
  from a worker and does nothing but post; teardown drops it, expires a life flag so a post already in
  Qt's queue is inert, and destroys the scheduler **before** the backend, because joining the worker
  is what guarantees nothing is inside the engine when it goes.
- **Six negative controls were run and ONE OF THEM REFUSED TO FAIL** — the case was fixed, not the
  code. The eviction claim was bounded by a tile COUNT loose enough that a `TilePresenter` which never
  evicted anything still passed: after scrolling from the top of a twelve-page document to the bottom,
  a never-evicting presenter holds roughly twice a viewport's worth, which satisfies any count bound
  loose enough not to be brittle. `TilePresenter::heldPages()` exists because of it, and the case now
  asserts the **first page's tiles are gone**. Convention 7, met by its own rule.

**T4 shipped the corpus (2026-09-02): 60 files, all generated, all reproducible.** `corpus/` now
holds **29 `ordinary/`**, **24 `malformed/`** with one named fault each, **5 `encrypted/`** across
revisions 2/3/4/6, and the two Stage 0 / T3 seeds — plus `PROVENANCE.md`, which gives every one of
them a row. Four things about T4 bind what comes next:

- **`python corpus/make-corpus.py --verify` is a gate**, `gates.corpus.reproducible`, with
  `--check-provenance` and a `--self-test` negative control beside it (guide cases **A-76 … A-78**,
  added at T4 because the task's exit criterion said reproducibility was *"asserted rather than
  assumed"* and nothing asserted it). **Editing a corpus file by hand now fails a test** — which is
  what `corpus/make-minimal.py` has been warning about since Stage 0 with nothing to enforce it.
- **The corpus has its own codecs, and they are first-party for a measured reason.** `zlib.compress`
  is not deterministic *across builds of zlib*, so `corpus/deflate.py` is ours; `math.cos` is the
  platform's libm, so `corpus/jpegenc.py` pins its DCT table. A generator that is deterministic only
  on this machine would turn the gate red on the Ubuntu runner for a reason unrelated to this
  project.
- **`ordinary/image-jpx.pdf` decodes to two tones, and that is correct.** `corpus/jpxenc.py`
  implements the MQ coder and the EBCOT **cleanup** pass only, so the codestream is *truncated*
  rather than malformed and a 0/255 source comes back as roughly 0/128. It exercises the
  `/JPXDecode` path; it is not a claim about JPEG 2000 fidelity. Same shape for the fonts: sixteen
  rectilinear glyphs exercise `/FontFile2` and Identity-H, not typography.
- **T8, T9 and T17 inherit named fixtures rather than assumptions.** A-17's lying `/Length` has a
  control that differs from it in **three bytes**; A-20's three-generation incremental file keeps its
  earlier revisions verbatim; A-21's *no hang* and *no unbounded allocation* clauses each have a file
  (`circular-prev`, `pages-loop`, `deep-nesting`, `huge-length-claim`). Measured at T4: all 24
  malformed files run through real PDFium with **no crash and no hang** — 19 recovered, 5 refused.

**T5 shipped `pdf/cos/Lexer` (2026-09-02) — the first PDF byte this project reads for itself.**
`rawpage_cos` now tokenizes; `pdf::createBackend()` still refuses everything by name, and no document
has been opened. Four things bind what comes next:

- ***Tolerant by design* is three guarantees, not a disposition.** It **never fails** — there is no
  `Result` in the lexer, and a malformed construct produces a token carrying a `LexNote` while lexing
  continues, because T8's `XrefRepair` cannot reconstruct anything from a reader that stops at the
  first bad byte. It **always makes progress** — every `next()` below the end advances at least one
  byte, which is the invariant that turns a fuzz run into a bounded program and the only one an
  explicit assertion catches, since its failure mode is a hang rather than a fault. And it **never
  reads out of bounds**, including inside the one-byte-from-the-end escapes `#`, `\` and `<`.
- **Every token carries its raw byte span *and* its decoded text, and that is not redundant.**
  `(a\053b)` and `(a+b)` decode identically and are not the same bytes; Stage 3 reproduces untouched
  objects verbatim, so something has to remember the exact spelling. **T6's `CosObject` inherits that
  requirement** — identity-preserving round-trip of anything we do not interpret.
- **Each tolerance has a corpus file or a named producer behind it**, and is asserted as a specified
  behaviour: `--5` reads as −5, `1.2.3` stays one token, `/A#GG` keeps its `#`, `<41A>` pads,
  a raw CR/CRLF inside a literal string normalizes to LF, and an out-of-range integer **clamps rather
  than wraps** — a wrapped length is negative, and a negative length is one `size_t` cast from an
  allocation the size of the address space.
- **`portable-fuzz` has its first target and has still never been run.** `fuzz_cos_lexer` exists;
  there is no clang on this machine, so **A-25's ten clean minutes is unrun and is recorded as a Linux
  job beside A-04** — not as pending. What keeps it honest meanwhile: its body lives in
  `tests/fuzz/LexerFuzzBody.h` and `tst_cos_lexer` replays all 60 corpus files through the *same*
  function (38 produce a note), and the entry point is compile-checked on Windows every run. **Neither
  is fuzzing** — 60 known files find nothing new by construction. `tests/fuzz/CMakeLists.txt` now
  **fails configure on the wrong target count** (1 at T5, **2 since T6**), which is A-27's *"zero is a
  failure, not a pass"* made structural.

**T6 shipped `pdf/cos/CosObject` and `pdf/cos/Parser` (2026-09-03) — the first PDF object.**
`rawpage_cos` turns bytes into objects. **At T6 no document had yet been opened** — that is T9's,
below — and five things it settled bind what came after:

- **Identity-preserving round-trip is carried by a SPAN, and the corollary is enforced rather than
  documented.** Every object parsed from a buffer records the exact byte range it was written as, so
  Stage 3's incremental save rewrites an untouched object by **copying** rather than by
  re-serializing — which is what keeps `(a\053b)` from becoming `(a+b)` and `4.` from becoming `4.0`.
  A **constructed** object has no span, and neither does one that has been **changed**: `append`,
  `insert` and `setStreamData` each clear it. **Stage 3 must read "no span" as *serialize it
  properly*, never as *nothing to write*.** Asserted the strong way: every object in every one of the
  29 `corpus/ordinary/` files is re-parsed from its own recorded range and must compare equal.
- **The `/Length` rule is: trust it only when it lands on `endstream`.** Absent, non-integer, an
  unresolvable `n g R`, past the end of the buffer, or simply wrong — all take a forward scan, and
  `CosStreamData` keeps **both** the declared and the real length because T8's report has to say what
  it reconstructed. Nothing weaker would do: `length-lies-long.pdf` is 400 bytes too long and
  **still fits inside the file**. A2's control is `ordinary/stream-length-control.pdf`, three bytes
  from its damaged twin, and it must parse with the recovery path **not** taken.
- **The parser cannot be made to overflow its stack, and that is a REFUSAL rather than a correctness
  claim.** A legal PDF has no depth limit; `corpus/malformed/deep-nesting.pdf` nests 4,000 levels and
  A-21 counts a stack overflow as a crash. `Parser::kMaxDepth` is **128**, and past it the construct
  is consumed **iteratively**, so the parse resumes at the right place instead of reading 3,872
  stray brackets as top-level junk. Measured: raising the bound turns two cases into a **SegFault**.
- **T7 inherits two seams that were left open on purpose.** An indirect `/Length 9 0 R` resolves
  through a **callback**, not an `XrefTable` member, because T7 is built on `Parser` and a member
  would be a cycle. And `parseObjectStream` takes the **decoded** payload: every `/ObjStm` in the
  corpus is `/FlateDecode`d, so T6's object-stream cases run on hand-built payloads and **the suite
  asserts that gap** rather than describing it, so it closes when T7's Flate closes it.
- **`portable-fuzz` now has both its targets and has still never been run.** `fuzz_cos_parser` is
  the primary one, `RAWPAGE_EXPECTED_FUZZ_TARGETS` went 1 → 2 exactly as T5 said it would, and
  **A-27 is closed**. A-25 and A-26 are Linux jobs beside A-04, not pending boxes. Replaying 60
  known files through `ParserFuzzBody.h` is **not fuzzing** and the suite says so at the assertion.

**T7 and T8 shipped the rest of the reader (2026-09-04), and the Stage 2 exit criterion with it.**
`rawpage_cos` now decodes streams, finds objects by number, and opens damaged files. Six things bind
what comes next:

- **zlib is not an image dependency — it is why the reader can open a modern document at all.**
  Every PDF 1.5-or-later file keeps its cross-reference table and its object streams
  Flate-compressed, so `/FlateDecode` is a prerequisite for finding a single object. That is why the
  filters had to land *with* the xref rather than after it, and why `codecs` is now on for the
  **portable** presets too. **P-3 stopped being a dry-run**: the real install took 1.2 minutes.
- **`/JBIG2Decode` is DECLARED AND DECODES NOTHING — issue I-06, a scope finding rather than a
  defect.** The only usable library is Ghostscript's `jbig2dec`, which is **AGPL-3.0** and therefore
  a build failure under D4 rather than a trade-off to weigh; **the corpus has no JBIG2 file at
  all**; and nothing in Stage 2 would call it, since rendering goes through PDFium. That is D2-4's
  own argument for `CryptFilter`. It refuses under its own catalog key rather than returning an
  empty buffer, which would reach the image pipeline as a blank page. `/CCITTFaxDecode` with
  `K > 0` is the smaller half: implemented, no fixture. **Finishing JBIG2 is ~450 lines plus a
  first-party `corpus/jbig2enc.py`, and the corpus comes first** — the decoder cannot be believed
  without the fixture.
- **A-18's criterion was unsatisfiable for three of the nine, and was corrected rather than
  narrowed.** `encode`→`decode` returning the input exactly is not a property JPEG or JPEG 2000
  *has*. `isLossless(FilterKind)` carries the distinction, the round-trip case iterates it, and the
  other four are asserted to **refuse to encode by name**.
- **`XrefRepair` repairs as LITTLE as possible, and `openWithRepair()` is deliberately the only
  entry point.** Rebuilding everything on a broken xref throws away the good half of a file whose
  damage is confined to its tail — most damaged files — and makes A-22's ratio 1.0 for every
  document ever opened. T9 opens documents through this one call, damaged or not: two paths is how
  the repaired case ends up untested. An **encrypted** file is declined here by name (D2-6) with
  `Unsupported`, never `ParseFailure`.
- **`RepairReport` is not a boolean, and Stage 3 depends on that.** *"Reconstructed 3 objects of
  4,000"* and *"reconstructed the entire xref"* are different risks; the from-scratch case is an
  explicit flag rather than an inference from the ratio. Its one-line summary is a **catalog key
  plus arguments**, never a sentence — `core/` has no `tr()`, and an English summary here is the
  single change that makes the product unshippable in `zh-TW` while looking reasonable in review.
- **`core/doc/` now holds three of D2-5's four types** — `Revision`, `Scope`, `RepairReport`.
  `Page` and `PageTree` are **T9's**, and a *fifth* type appearing there during Stage 2 is still a
  finding. `Scope` rounds **outwards** by rule: claiming more than was touched wastes a repaint,
  claiming less shows a stale tile.

**T9 shipped the thing the stage is named for (2026-09-04): a document opens and a page is drawn.**
`pdf::createBackend()` returns a backend that implements **2 of 7** interfaces where it reported 0
from T1, and `rawpage.exe` opens a real PDF. Six things bind what comes next:

- **`pdf::createBackend()` MOVED to `rawpage_pdfium`, and `tst_backend.cpp` split with it.** A
  backend that opens documents *is* the engine, and `tests/unit` links `rawpage_cos` under
  `rawpage_assert_headless()`, which forbids `rawpage_pdfium` by name (D2-2). The portable half of
  the census — the interface declarations, `notImplementedError`, `RasterImage`'s stride check —
  stays in the headless suite over a **test backend defined in that file**; the shipped backend's
  census is now in `tests/render/`. **What is lost is stated rather than absorbed: the concrete
  backend is no longer asserted under ASan.**
- **`core/render/RenderRequest.h` came forward from T10**, because `IPageRasterizer::raster()` takes
  one and `core/backend.h` has carried a forward declaration since T1. **T10 still owns**
  `DpiPolicy`, `TileGrid` and `PagePresentation`, which are built on it. Its equality IS the cache
  key (D2-12): no pointers, and the `Revision` is mandatory rather than defaulted, enforced by
  `isValid()` rather than by a comment.
- **`FPDF_GetPageCount` returns `/Count` VERBATIM.** Measured at T9:
  `corpus/malformed/pages-count-wrong.pdf` opens through PDFium claiming **97 pages over three real
  kids**. The page count reaches the status bar and the scrollbar before anything walks the
  document, so the tree is authoritative — `PdfiumDocument` drops the **trailing** run of pages that
  will not load, and only the trailing run, because removing an interior page renumbers every page
  after it and a link to page 40 would land on 39.
- **PDFium repairs silently and reports nothing.** A document it opened may have had its whole
  cross-reference table reconstructed with no sign of it, which is precisely the risk Stage 3 has to
  act on before allowing a plain Save. So `PdfiumDocument` runs the **first-party COS reader over
  the same bytes** for its `RepairReport` alone and discards its table — which is why T8's
  `XrefRepair` is not redundant with an engine that already opens damaged files.
- **The tile arithmetic is the thing most likely to be wrong.** `FPDF_RenderPageBitmap` has no
  region parameter: a tile is rendered by passing the **whole scaled page's size** with a
  **negative origin**. Passing the tile's size instead scales the entire page into every tile — a
  bug that reads as a zoom problem. It has a case of its own, and a negative control.
- **`Services` now carries `core::IDocumentBackend&`**, and `openFiles()` asks it instead of
  looking at the suffix (**A-47**, which replaces Stage 1's A-58 — that case asserted the opposite
  and was correct then under D1-2). A PDF saved as `.dat` opens; a `.pdf` that is a spreadsheet is
  refused **with the reason the engine gave**. Existence stays the shell's own check, because *"no
  such file"* and *"not a PDF"* are different messages.

**PDFIUM CANNOT BE CALLED FROM TWO THREADS AT ONCE, AND T11 SETTLED WHAT TO DO ABOUT IT.** Measured
at T3 by the A2 probe — eight workers, each with its **own `FPDF_DOCUMENT` and own `FPDF_PAGE`**,
which is exactly the confinement `D2-9` specifies, and they corrupt the heap; **1 worker exits 0, and
2, 4 and 8 all die with `STATUS_HEAP_CORRUPTION`**. What PDFium shares between threads is
process-global (its font mapper and glyph caches), not per-document, so confining a document to a
worker confines the wrong thing. Reproduce it with
`rawpage_bench_render.exe 8 --unsafe-parallel`. **A2 is unaffected and is `go`** — this is a
throughput ceiling, not a latency one: eight workers behind one engine lock retire **1.05×** the
tiles per second of one worker, while per-tile p95 stays at **0.532 ms** against a 16.667 ms budget.
D10's actual promise — *the UI thread never parses or rasterizes* — survives either **one render
thread with a queue** or **N workers serialized on one engine lock**. **T11 chose the first**, on that
measurement rather than on taste: the parallelism buys 5 % while the engine is already thirty times
inside the frame budget, and one consumer makes cancellation provable and PDFium's single-threadedness
structural rather than a discipline every later caller has to remember.
`RenderScheduler::Config::workerCount` remains and defaults to **1**, each worker taking its own
`IPageRasterizer` from a factory, because T18 re-measures A2 through this pipeline and must be able to
walk the count without a rebuild. The engine lock in `PdfiumInit` stays — it is correct under either
arrangement and it is what **A-45** passes on. **I-05 is CLOSED** (Stage 2 guide's §8) and D2-9's row
was corrected in the same change, exactly as that row said it would be.

**T10 and T11 shipped the whole of `core/render/` (2026-09-04) — every decision about a tile except
its pixels.** Six things bind what comes next:

- **`DpiPolicy` takes THREE inputs because Actual Size is physical.** The raster DPI is the
  monitor's real glass DPI times the zoom — a ruler on the screen must agree with the ruler on the
  page — and the OS scale factor is carried out separately as a device-pixel ratio. The consequence
  is a real property with a case: **moving a window between two monitors of equal glass DPI and
  different scale factors is a resample, not a re-render.** On the commodity desktop
  `physicalDpi == 96 * scale`, so the two are indistinguishable there and the control is the pair of
  monitors that differ. `requiresRerender()` divides the larger DPI by the smaller, so it is
  symmetric by construction — hysteresis here means two monitors that disagree about which is sharp.
- **`Resolution` carries an `upscale`, and forgetting it is a zoom bug rather than a cap bug.**
  6400 % on a 192-DPI monitor asks for 12,288 DPI and is capped at 2,400; `dpi * upscale` reproduces
  the request exactly, `TileGrid` tiles the RASTER size and `PagePresentation` lays out the
  PRESENTED size. Capping without the compensating factor silently shrinks the page.
- **`TileGrid` never enumerates the page**, and that is the whole of A-30's *no allocation
  proportional to the square of the zoom*. An A4 page at 6400 % is 14.6 gigapixels and 222,000
  tiles; the index range the viewport covers is computed rather than filtered out of a built list,
  so the returned set is bounded by the viewport. The control is the same viewport over a coarser
  page returning the same count. **`performance/prefetchPages` counts PAGES and this class sees
  one page** — within a page the knob is a ring of tiles, and the pages half is T13's.
- **`PagePresentation` handles both of plan §5.5's asymmetries with ONE rule**, because two special
  cases is where the off-by-one lives: pages group into spreads (a cover takes one of its own,
  shifting every later pair's parity), a page's column is its position in its spread and **a missing
  partner leaves its column empty** rather than sliding its neighbour across, and RTL maps column 0
  to the right and changes nothing else — not the grouping, not the document order. Its signature
  gained a **current page**, without which Single and Two-Up are indistinguishable from their
  continuous twins and the four modes are two.
- **`RenderCache::invalidate()` DROPS what the scope names and RE-KEYS the rest**, and the second
  half is the one that is not obvious. A `RenderRequest`'s equality is the key and it carries a
  `Revision`, so a cache that only dropped would lose every tile the moment a revision moved —
  `Scope` would buy nothing, and rotating page 3 of a 900-page document would re-render all 900 with
  nothing wrong on screen and no test failing. It takes a **`DocumentHandle`** the plan's signature
  omits, because a `Scope` names pages and not documents and **O7** leaves open whether one cache
  serves several. It is **not thread-safe on purpose**: only the thread that pumps
  `RenderScheduler::deliverReady()` ever touches it.
- **Delivery is asserted more strongly than "on the UI thread", because offscreen there is not
  one.** No subscriber is ever entered from a worker — a worker's only outward call is a data-free
  wake handler — checked by recording the thread id at every callback, with the wake as the control
  proving a worker existed at all. **Two of T11's five negative controls passed and should not
  have**: one was aimed at a line that was not the filter, and the other found that a job cancelled
  or superseded **while running** took a path no case could reach, because everything in the suite
  had either not started or already finished. A latch in the fake rasterizer now holds a tile inside
  the engine so a case can act mid-flight.

**T2 and T3 shipped PDFium and A2's verdict (2026-09-01).** `cmake/PdfiumAssert.cmake` pins the
engine at **153.0.8009.0 with V8 and XFA off** and fails configure on a mismatch naming both
versions (PA-1 … PA-5, seven gate cases over committed fixture trees, so they run with no PDFium
fetched); `pdf/render/PdfiumInit` owns the library's global state and nothing else touches it;
`pdfium.dll` is deployed beside the executable and `--version` reports the pin. `layer-lint`'s
`FPDF_` rule narrowed from `pdf/` to **`pdf/render/`** and gained a companion **`fpdf-handle`** rule
— which is not redundant, because PDFium's `include/cpp/` spells `ScopedFPDFDocument` with **no
underscore after `FPDF`** and slips straight past a search for `FPDF_`. `tools/spike-pdfium/` is
deleted. A fourth Windows-only test target, **`rawpage_render_tests`** (`render.` prefix), links the
engine so `tests/unit` never has to. **A2's probe figure: p95 0.532 ms against 16.667 ms — `go`** —
recorded in `Tech-Stack-Plan.md` §3.3 with risk **R5** struck through; the product's figure is still
owed at T18 (D2-1).

**T1 shipped the engine boundary and the library split (2026-09-01).** `core/backend.h` declares all
**seven** interfaces — `IDocumentBackend`, `IPageRasterizer`, `ITextExtractor`, `MutationSink`,
`ISigner`, `IOcrEngine`, `IIndexer` (D2-7) — and an unimplemented one returns `notImplementedError()`,
an `Unsupported` `Result` naming the interface and the method, never a silent success.
`pdf::createBackend()` returns a backend that implements **none** of the seven and says so:
`app/CompositionRoot` builds it and logs `backend: assembled -- 0 of 7 interfaces implemented`. Four
things about T1 bind what comes next:

- **`rawpage_pdf` is now `rawpage_cos`** (portable, first-party: `pdf/cos/` plus `pdf/BackendFactory`),
  and **`rawpage_pdfium` arrived at T2** with `pdf/render/PdfiumInit`. `rawpage_assert_headless()` —
  in `cmake/RawPageHeadless.cmake` so a gate fixture could exercise the real function — forbids the
  **name**, before resolving it, which is what makes the rule hold on the portable presets where that
  target will never exist (D2-2).
- **What crosses the engine boundary is buffers and callbacks.** `IPageRasterizer` returns a
  first-party `core::RasterImage` (width, height, **stride**, pixel format, bytes) and **not a
  `QImage`**: `core/` may not name a QtGui type, and `ui/` wraps the bytes at the far side.
  `Implementation-Plan.md`'s component list said `QImage` in three places and was corrected — issue
  **I-03** in the Stage 2 guide's §8. **T10 and T11 inherit that correction** for `RenderCache` and
  `RenderScheduler`.
- **A2 is measured twice**, a throwaway probe at T3 and the shipped pipeline at T18, which is Stage 1
  T15's R-9 lesson written down in advance (D2-1).
- **Four `core/doc/` vocabulary types come forward from Stage 3** — `Revision.h`, `Scope`, `Page` and a
  read-only `PageTree` — because three Stage 2 signatures already name them (D2-5). **A fifth type
  appearing in `core/doc/` during Stage 2 is a finding.**

Its two planning documents were written before any of that code, which is the workflow's intended
state: `Stage2-Rendering-Plan.md` (decisions **D2-1 … D2-14**, tasks **T1 … T18**, preconditions
**P-1 … P-4**, all four closed) and `Stage2-Rendering-Testing-Guide.md` (**79 automated**, **10
manual** cases), both dated 2026-08-31 — the seventy-ninth added at T13 (see **I-09**). **As of T13
the automated cases through A-56 have results, plus A-60 … A-62** — A-28 … A-34 at T10, A-36 … A-41
at T11, **A-39's widget half and A-52 … A-56 at T13**, **A-60 at T12**. **A-61 and A-62 are 🟡, and
each names the half it does not claim**: a real mid-session scale change is manual case M-02's, and
the battery throttle is T15's, because the settings key it names does not exist yet.
**The first four manual cases became walkable at T13** — there is a page on screen; M-05 … M-10 still
need golden baselines (T17), fonts (T16), the settings keys (T15) or Dr. Memory. Four cases are Linux
jobs this machine does not run (**A-04, A-24, A-25, A-26**) and are recorded as such rather than as
pending. The guide's issue log holds **nine**, of which **three are open**: **I-02** (S2, T7/T16 —
`license-audit` inventories the declared manifest rather than the resolved closure), **I-04** (S3,
T14) and **I-06** (S3 — JBIG2 is declared and decodes nothing). Closed: **I-01** and **I-03** (S3,
before any code), **I-05** (S2, at T11 — the arrangement D2-9 left open), **I-07** (S3, at T12 — a
`platform.h` comment that described the DPI-unaware failure mode as the specification), and **I-08**
(S2) and **I-09** (S3) at T13.

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
- **`tools/i18n-lint/` + `i18n/` (T6, extended T8/T9/T10/T11/T12, and S2 T7/T8/T9/T13)** — the third gate, and `rawpage_en.ts` /
  `rawpage_zh_TW.ts` at **565 keys each** — re-counted at every task that touches them rather than
  carried forward: 361 `action.*` + **33 `shell.*`** + 70 `ribbon.*`
  (nine of them the 2026-08-31 `ribbon.keytip.*` access keys) + 62 generated `prefs.*` + **39
  `error.*`** (T7 added nine for the filters and the xref, T8 six for the repair report and the
  encrypted decline, T9 five for the password prompt and the rasterizer, **T13 five** — one for a tile
  whose document closed while it was queued, four for the T12 platform interfaces).
  **T13 also DELETED a key**, which is the first time this project has: `shell.document.placeholderBody`
  said *"the PDF engine and the page surface arrive with Stage 2"*, and a key that apologizes for
  something that now exists is worse than no key — so `shell.*` went from 34 to 33 in the same change
  as the stub it described. The count was 538 with 11 `error.*` at Stage 2 T1; **T2 added
  `error.engine.initFailed` with `PdfiumInit` and no running figure was updated**, which is the same
  shape of drift the T1 recount had just corrected — the fix for a stale number is to re-measure it,
  not to remember it harder. **T6 added `error.pdf.badObjectHeader` and `error.pdf.badObjectStream`
  in both shipped locales and moved the figure in the same change**, which is that lesson applied.
  29 of `zh_TW`'s 565 are marked unfinished. Declared in
  `i18n/locales.json` with `zh_CN` and `ja` marked `"shipped": false`. The `.ts` files are compiled
  to `.qm` **beside the executable** rather than into a Qt resource, so Stage 15 can ship them
  replaceable; `LinguistTools` is a build tool and is deliberately not registered with
  `rawpage_link_qt`, because `qt-modules.txt` is the LINK closure `license-audit` audits.
- **`platform/` (T7, and S2 · T12)** — `platform.h` declaring all **seventeen** OS interfaces (the figure was
  carried as fifteen until T7 measured it), `PlatformFactory`, and **four** implementations.
  Stage 1's two: `WinSingleInstanceBus` (D1-11 — `Local\` mutex, a named pipe whose name carries the
  session id, and `AllowSetForegroundWindow` from the secondary) and `WinShellIntegration` (per-user
  PDF association only). **Stage 2 T12's two**: `WinDisplayTopology`, which reports each monitor's
  scale factor and its **raw glass DPI separately** — 163 DPI at 150 % on the P-1 rig's 4K panel,
  where the nominal `96 * scale` says 144 — and fires `changed` on a real difference rather than per
  message; and `WinPowerSession`, where **"no battery" is a distinct answer from "on mains"** and a
  UPS reports as mains. Both sit on `WinMessageWindow`, a hidden **top-level** window (not
  `HWND_MESSAGE`, which receives no broadcasts) because on Windows a notification is a window message
  and `platform/` may not own a `QWidget`. An unimplemented interface is a **null member**, never a
  silent stub. `tests/platform/` is a third test target, for the reason D1-4 gives about `ui/`.
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
- **Document tabs and what they carry (T12; the stub filled in at S2 · T13)** — `DocumentTabBar`
  (reorder, middle-click close, tear-off, re-dock, overflow) and `DocumentWindow`, which under
  **D1-2** held a path and nothing else until Stage 2 T13 gave it the open `DocumentHandle`, the
  document's `Revision` and a `PageView` — **and made it the thing that CLOSES the handle**, in a
  three-step protocol (cancel, drain, drop the tiles, close) because a worker inside PDFium on a
  document about to be destroyed is a use-after-free. **A tear-off is reported on the release, never at the 40 px threshold crossing** (I-34), and
  `detachDocument` refuses to detach a window's only document into a new window while still allowing
  it to be dropped onto another window's strip (I-33) — the guard sits *after* that branch, because a
  torn-off window holds exactly one document by construction. A window emptied by a **move** closes
  itself (I-35), and one emptied by a **close** does not; only a window that owns itself can go, since
  the primary window is a stack object in `app/main.cpp` whose address `main()` holds. `openFiles()` opens a tab per accepted path. **At Stage 1 it checked the name and the suffix and read
  no bytes (D1-2, case A-58); Stage 2 T9 replaced that with `IDocumentBackend::open()`** — a PDF
  saved as `.dat` now opens, and a `.pdf` that is not one is refused with the engine's own reason
  (A-47). Existence stays the shell's own check, because *"no such file"* and *"not a PDF"* are
  different messages. **`rawpage.exe` opens real PDFs into tabs and DRAWS THEIR PAGES** as of S2 · T13.
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

- **`core/render/` — the render policy layer (S2 · T10, T11)** — `DpiPolicy` + `Resolution`,
  `TileGrid`, `PagePresentation` + `PageLayout`, `RenderCache` with `ITileStore` /
  `MemoryTileStore` / `DiskTileStore`, and `RenderScheduler`. **All of it headless**, which is what
  keeps `rawpage_assert_headless()`'s claim true while the layer that drives the engine grows. Two
  things about it are worth knowing before touching it: `PageLayout::pageSlots()` is spelled that way
  because **Qt defines `slots` as an empty macro** and a method by that name does not compile (the
  `emit` collision T7 hit, in a second spelling); and `DiskTileStore` is the **only file in `core/`
  outside `core/settings/` that writes to disk**, carrying exactly two `layer-lint` waivers because
  every deletion in it goes through one helper.
- **`ui/viewer/` — the page surface (S2 · T13)** — `ViewportController`, `PageLayoutEngine`,
  `TilePresenter` and `PageView`, plus `app/BackendRasterizer.h`. The split is by QUESTION rather than
  by size, and two of the four are worth knowing before touching them. `ViewportController` **names no
  widget**: zoom-about-the-cursor, the scroll clamp and the view history are arithmetic over four
  numbers, so all thirteen of its cases run with no window — and its zoom assertion is an *equality*
  rather than a tolerance, because the content space scales uniformly. `TilePresenter` is **the only
  place in the product where a `core::RasterImage` becomes a `QImage`**, and it passes `bytesPerLine`
  rather than `width * 4`, because a rasterizer may align its rows and a page drawn over a padded
  buffer without its stride is sheared into diagonal garbage.
  `PageView` is a `QAbstractScrollArea` and deliberately not a `QScrollArea` — a 900-page document at
  6400 % is a 14-gigapixel widget and Qt lays out real widgets — and its zoom/scroll accessor is
  `controller()`, **not** `viewport()`, because the base class owns that name (the `PageLayout::
  pageSlots()` trap in a second spelling). **Content space is presented DEVICE pixels and Qt's is
  LOGICAL pixels**; the conversion lives in exactly two places and is equal on a 100 % monitor, which
  is why the offscreen suite cannot catch getting it wrong and manual case M-01 can.

- **The measurement, the two-process check, and the CI that runs them (T15)** — `tests/bench/`
  (`bench_ribbon`, A3's R-9: **p95 0.471 ms** against a 16.667 ms frame budget, reported in three
  phases and carrying **its own control**, because the `layout` phase reads 0.000 ms and a zero has
  two explanations — a cheap switch, or a dead instrument. Twenty resizes across the collapse
  threshold answer it at 0.714 ms); `tests/unit/tst_singleinstance.cpp` plus
  `scripts/check-single-instance.ps1` for A-61's two halves; and four CI changes that turn coverage
  claims into assertions. **T15 builds no product surface** — no new catalog key, settings key or
  registered command — and that is what the task is. `core/CoreModule.cpp` was deleted, as its own
  comment had asked since Stage 0.

**THE WHOLE READ PATH IS CONNECTED: A DOCUMENT OPENS AND A PAGE IS ON SCREEN. Nothing writes, and
nothing yet ACTS on what is shown.** `pdf/cos/` holds a tokenizer, an object model, a grammar, nine
filters, the cross-reference engine and the repair scanner — it can open a damaged PDF and hand back
any object by number. `pdf/render/` opens documents and rasterizes tiles. **`core/render/` decides
everything about a tile except its pixels**: the resolution to rasterize at, which tiles a viewport
needs, where each page sits, what is kept and what is thrown away, and in what order the work runs.
**`ui/viewer/` puts those tiles on a widget** (S2 · T13) and `platform/win/` tells it what monitor it
is on (T12). What nothing can do is emit a byte: `IncrementalWriter` and `ObjectAllocator` are
Stage 3's (D2-4). `core/doc/` holds five vocabulary types and no `Document`; `pdf/ops/` is still empty.

**What is NOT there yet, so it is not reported as a defect.** **No command acts on the page** — every
§8 Viewer chord resolves in the right context and finds nothing registered, which is `NotFound` and
deliberately leaves the key unconsumed (**T14**). The status bar still shows Stage 1's em dash for the
page total (**T14**), there is no `MessageBar` at all so a repaired file says nothing (**T14**), and
none of the sixteen `pageDisplay/` and `performance/` settings keys exists, so anti-aliasing, line
weight and the resolution cap cannot be changed (**T15**). A missing embedded font substitutes through
**PDFium's** policy rather than ours (**T16**), and `platform::IAcceleratedSurface` is declared and
unimplemented, so presentation is software (**T16**). What a person CAN do today: scroll with the bars
and the wheel, and **`Ctrl`+wheel to zoom about the cursor**, because a gesture is not a chord and
reaches the widget directly.

Task by task: T1 added the engine *contract* (`core/backend.h`) and a backend implementing none of
it; T2 `pdf/render/PdfiumInit`, the library's global state and nothing else; T4 **data** rather than
code (`corpus/` is 60 generated files and their generators); T5 `pdf/cos/Lexer`; T6 `CosObject` +
`Parser`; T7 the nine filters and `XrefTable`; T8 `XrefRepair` with the first three `core/doc/`
types; **T10 the arithmetic — `DpiPolicy`, `TileGrid`, `PagePresentation`** — and **T11 `RenderCache`
with its `MemoryTileStore` and `DiskTileStore` tiers plus `RenderScheduler`, which is where I-05 was
closed**; and **T9 `PdfiumDocument`, `PdfiumRasterizer` and `DocumentBackendImpl` — which is where a
page was first drawn by the product rather than by a benchmark**, and where
`pdf::createBackend()` stopped refusing every call by name; **T12 the two OS interfaces the policy
layer was waiting on**; and **T13 `ui/viewer/` — where a rendered tile finally reaches a widget.**
`platform/win/` holds **four** of seventeen interfaces. **`rawpage.exe` opens a window with a
working ribbon, a customizable toolbar, a tool finder, document tabs, a keyboard, a Preferences dialog
and a log pane** —
9 tabs above a sidebar, a status bar and a live theme, and **a tab per opened file showing the
document's pages**. Most ribbon buttons are **disabled and say why**: 361 commands are declared and
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

> **Every bare `Tn` in this section is a STAGE 1 task.** These conventions were written as Stage 1
> discovered them, and Stage 2 now has a `T1`…`T18` of its own — so an unqualified "T9" here means
> the shell task that built `MainWindow`, not the one that opened a document. That is the collision
> the *Citing a label* section warns about, applied to the one place in this file that predates it.

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
| `claude-plan/Stage<N>-<Area>-Testing-Guide.md` | How one stage is verified — **both** the automated pass I run and the manual pass you walk. **Three exist:** `Stage0-Toolchain-Testing-Guide.md` (run, signed off, closed); `Stage1-AppShell-Testing-Guide.md` (written before Stage 1's code, **fully run and fully walked** — 73 automated cases, 12 manual of which 9 were walkable and all 9 signed off, issues I-01…I-55, none open); and `Stage2-Rendering-Testing-Guide.md`, written from Stage 2's plan before its code — **78 automated (A-01…A-78) and 10 manual (M-01…M-10)**, of which **the automated cases through T9 have results and none of the ten manual ones is walkable yet** (there is no viewer to look at until T13). Four automated cases are Linux jobs this machine cannot run — **A-04, A-24, A-25, A-26** — and are recorded as such rather than as pending. Its one issue, **I-01**, was raised and closed before any code, by *reading* the document set rather than running it: `Architecture-FileMap.md` tagged the `pdf/cos/` reader and three `core/doc/` vocabulary types Stage 3 while `Implementation-Plan.md` — which owns stage ordering — claims them for Stage 2. **Stage 2's guide has its own `A-nn`/`M-nn`/`I-nn` namespace**; a bare `I-34` means Stage 1's. Each is written right after its stage's plan and before that stage's code, per the workflow section below. Do not write one for a stage that is not next; do not treat its absence as a gap. |

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

Layers are separate static libraries (`rawpage_core`, `rawpage_cos`, `rawpage_pdfium` — the last of those from Stage 2 T2 — `rawpage_platform`, `rawpage_ui`) linked
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
  (lexer, object model, xref, filters, crypt, incremental writer) and **links zlib, libjpeg-turbo
  and openjpeg from S2 · T7, all PRIVATE** so no `pdf/cos/` header names them; `pdf/render/` is the
  PDFium facade; `pdf/ops/` realizes each mutation.
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
| `tools/layer-lint` | ✅ **live** | an outward include; **`FPDF_` outside `pdf/render/`** — narrowed from `pdf/` at S2 T2 (D2-9), because `pdf/cos/` is first-party by rule and is half of what the portable presets build on Linux — and, as a companion rule, **a PDFium HANDLE named outside `pdf/render/`**, which catches `ScopedFPDFDocument` and the deleter family that contain no underscore after `FPDF` and slip past the first rule entirely; a filesystem write outside `pdf/`, `core/settings/`, `tools/` and `tests/` — the last two because a gate and a test fixture write by definition, and re-measuring the rule on 2026-09-02 found this line had been quoting only the first two, which reads as if a write in `tools/` were a violation; a path read out of `argv` outside `app/main.cpp`; **a `QAction` constructed outside `ui/shell/ActionFactory.cpp`** (and `RawPageAction.h`, which is the type the factory returns) (D1-7, four spellings, non-escapable); **an OS header in `platform/platform.h`** and **an engine or QtGui include in `core/backend.h`**, each its own layer entry because those files are included by three or four layers apiece |
| `tools/license-audit` | ✅ **live** | any dependency SPDX identifier outside the allowlist — **and from S2 · T7 it audits the RESOLVED CLOSURE**, enumerating every `share/*/vcpkg.spdx.json` in the installed tree rather than only what `vcpkg.json` declares (issue I-02). An installed tree *is* the closure, so this reads what we ship instead of keeping a second hand-maintained list; with **no** installed tree the enumeration is inert and the map still enforces the rule, which is what keeps the CI `gates` job honest. Two fixtures with a control prove it fires and does not simply fail on every tree |
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
ctest --preset x64-release --output-on-failure        # 598/598 as of Stage 2 T13, 1 skipped
```

The corpus is data, but it is **checked** like code — and the checks need Python, which is a
verification dependency and deliberately **not** a build one (`cmake --build` never calls it). They
run as `gates.corpus.*` in the `gates` preset; by hand:

```powershell
python corpus/make-corpus.py                     # regenerate all 60 files
python corpus/make-corpus.py --verify            # they regenerate BYTE-IDENTICALLY (D2-11, S2 T4)
python corpus/make-corpus.py --check-provenance  # every file has a PROVENANCE.md row, and vice versa
python corpus/make-corpus.py --self-test         # the negative control: each detection must fire
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
.\build\x64-release\bin\rawpage_bench_render.exe   # A2's probe figure (S2 T3). Add `8 --unsafe-parallel`
                                                    #   to reproduce I-05 -- it is EXPECTED to crash
.\scripts\check-single-instance.ps1                 # A-61's two-process half, with its `true` control
```

`check-single-instance.ps1` **edits `%APPDATA%\RawPage\settings.ini` and restores it in a `finally`**,
which is exactly why it is not a CTest case: a suite that rewrote the settings of whoever ran it would
be a worse defect than the one it checks. It has to, because Qt resolves `AppDataLocation` through
`SHGetKnownFolderPath` — measured at T15 — so overriding `APPDATA` for a child process does *not*
redirect the file.

```powershell
.\build\x64-release\bin\layer-lint.exe    --root . --explain   # includes, rules, 6 live waivers
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
`portable-fuzz`. **`portable-fuzz` has had targets since Stage 2 T5** — `fuzz_cos_lexer`, and
`fuzz_cos_parser` from T6, which is the primary one — and has still never been *run*: it needs clang
and libFuzzer, neither of which is on this machine, so guide cases A-25 and A-26 are Linux jobs like
A-04 rather than pending boxes. `tests/fuzz/CMakeLists.txt` fails configure when its target count is
wrong — **1 at T5, 2 from T6** — because "ten minutes with no crashes" is passed trivially by a
target that builds nothing.

**Four test targets and two benchmarks, and the split is load-bearing rather than organizational.**
Counted 2026-09-02; this heading read *"three"* from Stage 1 T9 until then, having missed
`rawpage_render_tests` (S2 · T2) and `rawpage_bench_render` (S2 · T3) even though the table below
already listed the first. A fifth suite, `gates.`, has **no binary at all** — its 90 cases drive the
gate executables and CMake itself through `cmake -P`, which is what lets the `gates` preset run it
with no Qt and no vcpkg. Current split of the 598: `unit.` 300, `ui.` 150, `gates.` 90,
`platform.` 28, `render.` 28, `bench.` 2. **One of the 598 is deliberately SKIPPED rather than green** —
`platform.powersession reports the machine's power source`, on a desktop with no battery, which is
guide case A-62's own *"report not applicable explicitly rather than pass vacuously"* honoured.

| Target | Links | CTest prefix | Runs where |
|---|---|---|---|
| `rawpage_tests` | `rawpage_core`, `rawpage_cos` | `unit.` | Windows + `portable-asan`. It also compiles **three** `app/` translation units **in** rather than linking them — `CommandLine.cpp` (a pure `QStringList` → intent parse), `LogFile.cpp` (QtCore and nothing else) and `SingleInstance.cpp` (T15; names only `core/` and the interface header `platform/platform.h`) — and compiling rather than linking is what keeps the assertion below true. `rawpage_assert_headless()` — in `cmake/RawPageHeadless.cmake` since Stage 2 T1 — **fails configure** if this ever links `rawpage_ui`, `rawpage_platform` or **`rawpage_pdfium`**, directly or transitively; that assertion is the standing proof that `core/` needs no display and no engine, and `tests/gates/fixtures/headless-assert/` is the five-case fixture proving it fires — with the control proving it does not fire on a clean closure. **From S2 · T5 it also holds the COS reader's suite**: `rawpage_cos` contains `pdf/cos/Lexer` and, from T6, `CosObject` + `Parser`, and `tst_cos_lexer` / `tst_cos_parser` reach `corpus/` through a `RAWPAGE_CORPUS_DIR` compile definition — the same mechanism as `RAWPAGE_FEATURES_MD`, and for the same reason: a test that guessed a relative path would pass or fail on whatever working directory CTest happened to use |
| `rawpage_platform_tests` | `rawpage_core`, `rawpage_platform` | `platform.` | **Windows only** (S1 · T7) |
| `rawpage_ui_tests` | `rawpage_core`, `rawpage_ui` | `ui.` | **Windows only**, `QT_QPA_PLATFORM=offscreen` (D1-4). **Arrived at T9**, not the plan's T15, on the precedent `tests/platform` set at T7. It installs `rawpage_en.qm` so the widgets are exercised against real text rather than their own catalog keys. **From S2 · T13 every fixture comes from `tests/ui/ShellFixture.h`** — seven copies of the same eight fields became a seven-way edit the moment `app::Services` grew a render pipeline — and that header supplies the FAKE rasterizer the viewer suite runs on, since this target may not link an engine. Its tile colour is chosen so that *a page is on screen* is distinguishable from *a white sheet was drawn*, and it pads its rows so the `bytesPerLine` assumption is exercised somewhere |
| `rawpage_render_tests` | `rawpage_core`, `rawpage_cos`, `rawpage_pdfium` | `render.` | **Windows only** (S2 · T2). **From S2 · T13 it also holds the only case that drives the REAL engine through the REAL scheduler** (`tst_renderpipeline.cpp`, guide A-79): every other suite fakes one end of that path — `tests/unit` and `tests/ui` may not link an engine, and the rest of this target calls the rasterizer with no scheduler — so the assembly the product actually runs had no case at all until then (issue **I-09**), which is why `app::BackendRasterizer` is a header rather than a class private to `CompositionRoot.cpp`. It links the ENGINE, which is the whole reason it is a separate target: the raster suite is the pressure that would otherwise put PDFium into the headless closure and destroy what `rawpage_assert_headless()` proves. It needs **no display** — there is no Qt GUI object in it — which is how "initializing PDFium does not require a display" is established rather than assumed. **From S2 · T9 it also holds the SHIPPED BACKEND's census**, which `tests/unit/tst_backend.cpp` used to assert: `pdf::createBackend()` moved into `rawpage_pdfium` when it started returning something that opens documents, and the headless suite may not link that. The portable half of A-46 stays in `tests/unit` over a test backend defined in that file |
| `rawpage_bench_ribbon` | `rawpage_core`, `rawpage_ui` | `bench.` | **Windows only**, offscreen (T15). Not a test target and the difference is load-bearing: a test asserts and fails, **a benchmark reports, and its exit code is about whether the measurement was *possible*** — never about whether the machine was fast, or a loaded runner turns a green build red for a reason unrelated to the change. It is registered with CTest anyway, because a benchmark nobody runs is one that has stopped compiling by the time its number matters |
| `rawpage_bench_render` | `rawpage_core`, `rawpage_cos`, `rawpage_pdfium` | `bench.` | **Windows only** (S2 · T3), and it creates **no Qt GUI object at all** — which is how "the engine needs no display" is established rather than assumed. A2's probe figure, and the run that found **I-05**. It takes an optional path, so any corpus file can be put through the real engine by hand; `8 --unsafe-parallel` reproduces the heap corruption on demand, because a finding that needs a source edit to reproduce is one that gets re-litigated |

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
