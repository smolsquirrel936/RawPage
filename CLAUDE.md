# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**This file is a ROUTING document.** It holds what binds every session — the locked stack, the
architecture, the machine-enforced rules, the commands — and points at the document that owns
everything else. It deliberately does **not** carry per-task history, per-file descriptions or live
status; those have owners, listed in the document-set table below, and a second copy here is how a
number goes stale. When something changes, **fix the owning document**; never re-decide a fact here.

## What this repository is

**RawPage** — a Windows-first desktop PDF editor modeled on Right PDF Professional. The design lives
under `claude-plan/`. Stage 0 (toolchain), Stage 1 (app shell), Stage 2 (PDF engine boundary and
tiled rendering), Stage 3 (mutation and save) and **Stage 4 (the viewing and navigation surface) are complete and signed off, the last on 2026-09-27**. Risk A4's probe verdict is `go` and its product verdict is recorded as partial, both over RawPage's own generated signatures. **A2 at Stage 4 is `go`** (`Tech-Stack-Plan.md` §3.3), and `platform::IAcceleratedSurface` is answered *not built*. **Stage 5 is in progress** — T1, the corpus, done 2026-09-29; T2, the text model, the extractor and the engine thread's text job, and T3, **A2 at Stage 5 — `go, with named custom work`** (re-graded **`go`** by the user at T14), both done 2026-09-30; T4, `TextNormalizer` and `LayoutAnalyzer` **meeting every layout target** on the generated corpus, 2026-09-30; T5, **selection and copy**, T6, **the text index and search** -- the second search of a 900-page file instant by the guide's §3.3 -- T7, **the find bar**, T8, **comments, form fields, bookmarks and attachments searched**, T9, **Advanced Search over a folder**, and T10, **the Snapshot** -- the region re-rendered at `pageDisplay/snapshotDpi`, 0 pixels from a fresh render -- all 2026-10-01; T11, **Reflow view** -- `columns-two.pdf` read as the generator's 16 blocks in order, the place kept both ways -- and T12, **`DocumentComparer`** -- every labelled change of `corpus/compare/` found with its kind and, at text only, nothing else -- and T13, **the comparison surface** -- side by side by aligned page, the overlay in two tints, `Ctrl+]`/`Ctrl+[` through both views, neither document changed -- and T14, **the re-measurement, the census and the lockstep** -- A2 at Stage 5 re-read through the shipped surfaces, every column inside the frame budget, and two windows that widened themselves fixed on the way (S5 guide I-10, I-11) -- 2026-10-02, **which makes Stage 5 CODE COMPLETE and not signed off**: its nine manual cases are walkable and none is walked. Its plan and testing guide were written 2026-09-29 and the plan's §0 answered.

### Where the work is right now — read this first

| | |
|---|---|
| **Stage** | **Stage 4 is SIGNED OFF by the user, 2026-09-27**, with every `Stage4-ViewingSurface-Testing-Guide.md` §9 item met — fourteen tasks (T1 2026-09-19 … T14 2026-09-23), 108 automated cases each carrying a result, and all twelve manual cases walked and signed off, the last (M-12) on 2026-09-27. **Stage 3 was signed off 2026-09-18**, Stage 2 2026-09-14, Stage 1 and Stage 0 before it. **CI over the signed-off tree:** run **36325491277**, on the sign-off commit `1128e7a`, **all five jobs green** (read 2026-09-29) — so CI's Windows job HAS run over I-31's change to `platform/win/WinDurableFiles.cpp`, which the five commits after run 36307895451 (`e739cb1`) had not been pushed for at sign-off. **Stage 4's one open issue is I-07 (S4)**, a page colour that can be inverted but not substituted, deferred by the user as `DW-1` (`Deferred-Work.md`) with no stage owning it yet. |
| **Next action** | **Stage 5's manual walk, M-01 … M-09** (`Stage5-TextSearch-Testing-Guide.md` §6) -- every case walkable; **M-01's first walk failed at step 2** (S5 guide **I-14**, a lost double click, closed the same day) and it **passed on its re-walk** that day; **M-02 passed** the same day, and S5 guide **I-15** (a selection barely visible on white paper) was raised after it and closed, as was **I-16** (the ribbon's key tips left on screen after a click or a focus change); M-03 … M-09 not walked -- **and then the user's sign-off** (guide §9, whose every other item is met). **CI over the T14 tree:** run **36998382841** on `85852f2`, all five jobs green, read from the logs; with it guide A-04 and A-06 are ticked and all 114 automated cases carry a result. **A2 at Stage 5 is `go`** -- re-graded by the user on 2026-10-02 from T14's measurement (`Tech-Stack-Plan.md` §3.3, *A2 at Stage 5, re-read through the shipped surfaces*): every column inside the budget through the shipped surfaces, and T3's own bench's folder search 13.99-14.38 ms on the T14 binary. The 2 priority inversions a run that bench then counted were not inversions (S5 guide **I-12**, closed after T14): a column's first job, picked before the first tile was queued and stamped after it. The count is now the scheduler's own, taken at the pick, and reads 0. The UI thread's 45-48 ms stalls during a folder search that T14 recorded were a 900-page index entry freed on the UI thread (S5 guide **I-13**, closed after T14): the index now frees on a thread of its own, and the longest stall reads 10.8-11.8 ms. |
| **Then** | Once Stage 5 is signed off: Stage 6's plan, then its testing guide, before any Stage 6 code (*Per-stage workflow* below). |
| **Authority for status** | The **Stage 0 to Stage 4 status blocks** in [`Implementation-Plan.md`](claude-plan/Implementation-Plan.md) §4 — task by task and deviation by deviation, with every issue — and each stage's testing guide, which owns its cases, results and issue log. **Read them rather than trusting a figure remembered from here.** |
| **Tests** | **Latest run, 2026-10-03 after the rulers' corner box became a button (the user's request, S5 guide A-115): 1399/1399 on `x64-release` and on `x64-debug`** — the +3 `unit.the next unit walks all five in order and wraps`, `ui.a press on the rulers' corner box steps every tab to the next unit and wraps` and `ui.the rulers' corner box draws its widest unit whole` — the same **four named skips**; `checks` clean; `rawpage_ui_tests.exe` whole in declaration order on both presets and under seed 20261003, 428 cases, 3 skipped; incremental builds, 0 `warning:`. *Before that,* **2026-10-02 after S5 guide I-16's fix (the ribbon's key tips dismissed by a focus change or a mouse press, raised by the user before M-03): 1396/1396 on `x64-release` and on `x64-debug`** — the +1 `ui.the key tips go when the focus leaves the ribbon or the mouse is pressed` — the same **four named skips**; `checks` clean; `rawpage_ui_tests.exe` whole in declaration order and under seed 20261002, 426 cases, 3 skipped; incremental builds. *Before that,* **2026-10-02 after S5 guide I-15's fix (the selection multiplied onto the page, raised by the user after M-02): 1395/1395 on `x64-release` and on `x64-debug`** — the +1 `ui.composition.a selection tints the paper clearly and keeps its glyphs at full contrast on either page colour` — the same **four named skips**; `checks` clean; `rawpage_ui_tests.exe` whole in declaration order, 425 cases, 3 skipped; incremental builds. *Before that,* **2026-10-02 after S5 guide I-14's fix (the page view's double click, raised by M-01's first walk): 1394/1394 on `x64-release` and on `x64-debug`** — the +1 `ui.a double click event is the next click of the gesture and not a lost one` — the same **four named skips**; `gates` **133/133**; `checks` clean; `rawpage_ui_tests.exe` whole in declaration order, 425 cases, 3 skipped; incremental builds. *Before that,* **2026-10-02 after S5 guide I-13's fix (the index's releaser): 1393/1393 on `x64-release` and on `x64-debug`** — the +1 `unit.a dropped evicted or invalidated document's pages are freed off the calling thread` — the same **four named skips**; `gates` **133/133**; `checks` clean; incremental builds, 0 `warning:`. *Before that,* **2026-10-02 after S5 guide I-12's fix (the scheduler's inversion count): 1392/1392 on `x64-release` and on `x64-debug`** — the +1 `unit.render scheduler counts a job started while a more urgent one waited and only then` — the same **four named skips**; `checks` clean; incremental builds, 0 `warning:`. *Before that,* **2026-10-02 at Stage 5 T14: 1391/1391 on `x64-release` and on `x64-debug`, each after a clean build** — the +2 `[findbar][I-10]` and `[comparison][I-11]` (S5 guide §8) — the same **four named skips**; `gates` **133/133**; `checks` clean, `mutation-lint` one command, `layer-lint` **15** escapes; `rawpage_ui_tests.exe` whole in declaration order and under three seeds on both presets, 424 cases, 3 skipped; both presets **0 `warning:` and 0 `error:` over 330 C++ and 6 resource objects**, logs kept. *Before that,* **2026-10-02 at Stage 5 T13: 1389/1389 on `x64-release` and on `x64-debug`** — the +16 `tst_comparison` (6), `tst_comparisonink` (7, `ui.composition.`), `tst_compare`'s reading-source case, `tst_stage5settings`' four-keys case and `gates.i18n_lint.action_ids_are_not_catalog_keys` (S5 guide I-08) — the same **four named skips**; `gates` **133/133**, every clause of a case now asserted (S5 guide I-09); `checks` clean, `mutation-lint` one command, `layer-lint` **15** escapes; `rawpage_ui_tests.exe` whole on both presets, 422 cases, 3 skipped; a clean `x64-debug` build, its log kept, 0 warnings over 329 objects. *Before that,* **2026-10-02 at Stage 5 T12: 1373/1373 on `x64-release` and on `x64-debug`** — the +17 `tst_compare` (8), `tst_layout`'s I-07 case and `tst_comparecorpus` (8, `render.`) — the same **four named skips**; `gates` **132/132**; `checks` clean, `mutation-lint` one command, `layer-lint` **15** escapes; `rawpage_ui_tests.exe` whole on both presets, 416 cases, 3 skipped; a clean `x64-debug` build, its log kept, 0 warnings over 324 objects. *Before that,* **2026-10-02 after Stage 5 guide I-06's fix (T11's reflow place on a turned page): 1356/1356 on `x64-release` and on `x64-debug`** — the +2 `unit.` and `ui.composition.` cases for every rotation — the same **four named skips**; `checks` clean; `rawpage_ui_tests.exe` whole on both presets, 416 cases, 3 skipped. *Before that,* **2026-10-02 at Stage 5 T11: 1354/1354 on `x64-release` and on `x64-debug`** — the +13 `tst_reflowtext` (5), `tst_reflow` (6) and `tst_reflowink` (2, `ui.composition.`) — the same **four named skips**; `gates` **132/132**; `checks` clean, `mutation-lint` one command, `layer-lint` **15** escapes; `rawpage_ui_tests.exe` whole on both presets, 416 cases, 3 skipped; a clean `x64-debug` build, its log kept, 0 warnings over 320 objects. *Before that,* **2026-10-01 at Stage 5 T10: 1341/1341 on `x64-release` and on `x64-debug`** — the +13 `tst_snapshot` (9), `tst_snapshotink` (3, `ui.composition.`) and the `pageDisplay/snapshotDpi` row in `tst_stage5settings` — the same **four named skips**; `gates` **132/132**; `checks` clean, `mutation-lint` one command, `layer-lint` **15** escapes; `rawpage_ui_tests.exe` whole on both presets, 410 cases, 3 skipped; a clean `x64-debug` build, its log kept, 0 warnings over 315 objects. *Before that,* **2026-10-01 at Stage 5 T9: 1328/1328 on `x64-release` and on `x64-debug`** — the +20 `tst_searchresults` (6), the scheduler's exclusive-task case, `tst_foldersearch` (7, `render.`), `tst_searchpane` (5) and A-80's `ui.composition.` case — the same **four named skips**; `gates` **132/132**; `checks` clean, `mutation-lint` one command, `layer-lint` **15** escapes; `rawpage_ui_tests.exe` whole on both presets, 401 cases, 3 skipped; a clean `x64-debug` build, its log kept, 0 warnings over 312 objects. **`bench_pipeline --stage5`'s folder column is NOT inside the budget** (19.2-19.4 ms, the viewer alone 15.1-15.9 on both the T8 and T9 binaries that day; `Tech-Stack-Plan.md` §3.3). *Before that,* **2026-10-01 at Stage 5 T8: 1308/1308 on `x64-release` and on `x64-debug`** — the +16 `tst_search` (2), `tst_index` (4), the scheduler's engine-task cases (2), `tst_embeddedfiles` (3), `tst_searchclasses` (4, `render.`) and A-77's `ui.composition.` case — the same **four named skips**; `gates` **132/132**; `checks` clean, `mutation-lint` one command, `layer-lint` 14 escapes; `rawpage_ui_tests.exe` whole on both presets, 396 cases, 3 skipped; a clean `x64-debug` build, its log kept, 0 warnings over 306 objects. *Before that,* **2026-10-01 at Stage 5 T7: 1292/1292 on `x64-release` and on `x64-debug`** — the +16 `tst_findbar` (12), two `ui.composition.` find cases (A-74's ink, A-21's report), the resolve case in `tst_shell` and `gates.i18n_lint.wrong_context` (S5 guide I-05) — the same **four named skips**; `gates` **132/132**; `checks` clean, `mutation-lint` one command; `rawpage_ui_tests.exe` whole on both presets, 396 cases, 3 skipped; a clean `x64-debug` build, its log kept, 0 warnings over 303 objects (an earlier clean build's unkept log counted 13 `warning:` lines that did not reproduce -- the T7 row says what is known). *Before that,* **2026-10-01 at Stage 5 T6: 1276/1276 on `x64-release` and on `x64-debug`** — the +48 `tst_search` (15), `tst_index` (19), `tst_stage5settings` (4), `unit.search_settings.restart` (1, two processes), `tst_searchcorpus` (5, `render.`) and `tst_documentindex` (4) — the same **four named skips**; `gates` **131/131**; `checks` clean, `mutation-lint` one command; `rawpage_ui_tests.exe` whole on both presets, 383 cases, 3 skipped; a clean `x64-debug` build, 0 warnings over 301 objects; **A-63: the second search of `text-900.pdf` p95 13.23 ms** against the guide's 100. *Before that,* **2026-10-01 at Stage 5 T5: 1228/1228 on `x64-release` and on `x64-debug`** — the +30 `tst_selection` (11), `tst_selecttool` (10), `tst_clipboard` (1) and `tst_selectionink` (8, `ui.composition.`) — **FOUR named skips**, the three below and S5 guide **A-50**, which skips offscreen and passed by hand on the `windows` platform (`[D5-10]`, 17 of 17); `gates` **131/131**; `checks` clean, `mutation-lint` one command; `rawpage_ui_tests.exe` whole on both presets, 379 cases, 3 skipped. *Before that,* **2026-09-30 at Stage 5 T4: 1198/1198 on `x64-release` and on `x64-debug`** — the +22 `tst_normalize` (6), `tst_layout` (8), `tst_layoutcorpus` (6) and two `gates.fuzz_count.*` cases — the same three named skips; `gates` **131/131**; `checks` clean. *Before that,* **2026-09-30 at Stage 5 T3: 1176/1176 on `x64-release` and on `x64-debug`** — the +4 the scheduler's cache-bypass case, I-01's and I-02's regression cases (S5 guide §8) and `bench.render.a2_stage5` (~50 s) — the same three named skips; `gates` **129/129**; `checks` clean with `mutation-lint` at one command. *Before that,* **2026-09-30 at Stage 5 T2: 1172/1172 on both presets** — the +23 `tst_textmodel` (5), the scheduler's text cases (7) and `tst_extract` (11, S5 guide A-17 … A-26). *Before that,* **2026-09-29 at Stage 5 T1: 1149/1149 on `x64-release` and on `x64-debug`** — the +2 `gates.corpus.faults` and `gates.corpus.compare` (S5 guide A-14, A-15) — the same three named skips; `gates` **129/129**; `checks` clean with `mutation-lint` at one command. *Before that,* **2026-09-27 after I-30's and I-31's fixes: 1147/1147 on `x64-release`** — the +4 three `tst_durablefiles` cases (A-107) and `gates.layer_lint.win32_setinfo_outside_allowed` — and **1147/1147 on `x64-debug`**, the same three named skips (S4 guide §9 item 4). *Before that,* **after a clean rebuild of both presets: 1143/1143 on `x64-debug`, and 1142 of 1143 on `x64-release`** — the one red Stage 3's A-68 on its COVERAGE floor, which was **I-30**, the kill harness's instants locked to the timer tick. *Before that,* **1143/1143** on `x64-release` and **1143/1143** on `x64-debug` (re-run 2026-09-27 after **I-28's and I-29's fixes, the +1** — A-104; A-103 is a listener, not a case — over that day's **1142**, which was re-run after **D4-22, `Alt`+wheel and the release-time key tips, the +3** — A-101's two cases and A-102 — over that day's 1139, which was **D4-20, the sharp lens, the +2** — A-99's two cases — over that day's 1137, which on `x64-release` had **ONE intermittent failure** — `platform.durable replace control`, a Stage 3 case this change does not touch, which then passed six times alone, cause not established; that 1137 was **I-26's and I-27's fixes, the +3** — A-97's two cases and A-98 — over that day's 1134, which was **I-24's and I-25's fixes, the +2** — A-95 and A-96 — over that day's 1132, which was **I-23's fix, the +1** — A-94 — over that day's 1131, which was **I-22's guard, the +2** — `gates.text_bytes.*` — over that day's 1129, which was **I-20's and I-21's fixes, the +1** — A-92; A-91 was rewritten — over that day's 1128, which was **I-18's and I-19's fixes, the +2** — A-90 and A-91 — over 2026-09-26's 1126, which was **the dotted guide preview, the +2** over I-17's 1124, which was +3 over I-16's 1121, itself +2 over I-15's 1119), **three** explicitly skipped — the battery case, on a desktop that has none, **A-85** (Stage 4 guide), which needs two monitors at different scale factors and passes on the `windows` platform instead, and, since 2026-09-27, **A-94**, which needs a real window frame and likewise passes on the `windows` platform — re-run 2026-09-26 after **I-14's fix, the +1** over 1116, which was **I-13's fix and D4-18's case, the +2** over that morning's 1114, which was **I-11's and I-12's fixes, whose six new cases are the +6** over 2026-09-24's 1108 (I-10's fix, +2 over T14's 1106), and re-run the same day with the walker's window CLOSED, after a first run with it open had skipped `ui.diagnostics_ignore_the_bus` and left the release `rawpage.exe` unlinked; 1098 after T12, 1069 before it, 1067 at T11. The clean-build warning census was last re-taken at Stage 5 T14 on **both** presets (**0 `warning:` and 0 `error:` over 330 C++ objects each**, 2026-10-02 — the 330th `app/Application.cpp` in `rawpage_bench_surface`), before that at T13 on `x64-debug` (0 over 329; 324 at T12) and on both presets on 2026-09-27 (**0 `warning:` and 0 `error:` lines**, 303 objects release and 292 debug — T14's 302 and 291 plus `ui/viewer/LoupeRaster.cpp`, S4 guide A-01) — an incremental build inherits a census rather than measuring it. The `gates` preset is **133/133** with no Qt since S5 T13 (2026-10-02, `gates.i18n_lint.action_ids_are_not_catalog_keys`), over 132/132 at S5 T7 (2026-10-01, `gates.i18n_lint.wrong_context`), over 131/131 at S5 T4 (the two `gates.fuzz_count.*` cases), 129/129 at S5 T1 (2026-09-29, the two corpus cases above) and 127/127 on 2026-09-27 (I-31's `win32_setinfo_outside_allowed` over I-22's two `gates.text_bytes.*` cases over T14's 124). **The two presets are not interchangeable**: T13's **I-09** fail-fasted twice in `x64-debug` on a line `x64-release` had just reported green over. Re-measure rather than quote: a count in prose goes stale silently. |

**What works today.** `rawpage.exe` opens real PDFs into tabs and draws their pages: a ribbon
(9 tabs, **49 groups**, 200 controls -- re-derived 2026-09-21 at S4 T11, and the group count is a
CORRECTION: 58 was the number of `"id":` lines in `res/ribbon.json`, which is 49 groups plus 9 tabs), a customizable Quick Access Toolbar, a tool finder, document tabs, a
keyboard, a Preferences dialog, a log pane and a live theme. A person can scroll, page through with
the keyboard, zoom by chord or `Ctrl`+wheel, fit four ways, rotate the view, switch the four layout
modes, type a page number, pan, marquee-zoom, and read a banner saying a file was repaired. The whole
**read** path is connected — `pdf/cos/` (lexer, objects, nine filters, xref, repair), `pdf/render/`
(the PDFium facade), `core/render/` (every decision about a tile except its pixels), `ui/viewer/`
(tiles onto a widget). **The save path exists below the UI** since Stage 3 T8 — `CommandJournal`,
`SaveCoordinator`, `pdf/ops/RevisionWriter` and `platform::IDurableFiles` save a rotation over the
whole editable corpus in tests — and **since T10 a person reaches it**: `Ctrl+R` rotates pages, `Ctrl+Z`/`Ctrl+Y` undo and redo,
`Ctrl+S`/`Ctrl+Shift+S` save, and closing a changed document asks first. **Since T11 a launch after a crash offers what was not saved**: recover it — into the file, or into an untitled document when the file changed — discard it, or decide later.
**And since Stage 4 T5 and T6 the sidebar holds three real panes.** Page thumbnails — one per page with its label, navigated by a click or `Enter`, walked with `↑`/`↓`, resized with `+`/`-`, rotated with `Ctrl+R` through Stage 3's command — rendered on the same scheduler and cache as the page, at `Speculative`, bounded to what the pane can show. Bookmarks, as the document's own tree with its own open/closed state, navigated by `Enter` and expanded with `→`/`←`/`*`/`/`. And named destinations, sortable by name or page, with one that goes nowhere shown as such rather than dropped. **All three navigate, and since T12 they navigate to the destination's VIEW and not only to its page**: `/XYZ`'s point and zoom, `/FitH`'s top, `/FitV`'s left, `/Fit`'s whole page and `/FitB`'s content box are applied, with a null in any of them meaning *keep what you have* as ISO 32000-1 §12.3.2.2 says — `/FitR`'s rectangle is the one thing still unread, because `core::Destination` has no fields for it (Stage 7's). That closed **I-05**, and applying it exposed a second defect worth knowing: **a fit re-satisfied against the page the viewport happens to be dominated by feeds back into the layout that decides which page that is**, so a bookmark to page 2 landed on page 4 until Fit Visible was anchored to the page it was set for. **And since T8 the page has something to measure it with**: two rulers in `units/rulerUnit`, reading the page from its drawn corner and tracking the cursor, whose corner box steps that unit to the next on a press (since 2026-10-03, `view.cycleRulerUnit`); a grid whose spacing, subdivisions, colour and origin are preferences; guides dragged off either ruler, moved, locked, deleted and cleared through commands; snapping to guides and the grid at **one threshold in DEVICE pixels**, so the tolerance is the same at 50 % and at 400 %; and `Ctrl+U`, `Ctrl+'` and `Ctrl+;` toggling the three of those that have a chord. **None of it is rasterized into a tile** — the grid, the guides and the page inversion are painted over the pixels the scheduler already returned, so changing the grid spacing re-renders nothing (D4-11). **And since T10 a document can be PRESENTED**: `F11` takes the window full screen and a *Presentation* command opens a surface of its own on a chosen monitor — one controller with two levels (D4-12), so both ask Windows to hold the display awake and every way out pairs that request. Pages turn with a transition the DOCUMENT chose where it has one and the preference chose where it does not, auto-advance steps at `/Dur` or at `fullScreen/autoAdvanceSec` and loops or stops, `B` and `W` blank the screen, `P` is a laser pointer, `Ctrl+L` hides the cursor and `Ctrl+Shift+M` moves it to the next display — and a monitor that disappears mid-slide moves the presentation rather than losing it. **And since T9 one document can be shown in up to four panes of one window**: split it with a horizontal divider, a vertical one, or a spreadsheet freeze that pins a top row and a left column while the body scrolls — over **one handle, one journal and one revision** (D4-13), so a `Ctrl+R` in either half rotates the page in both; the panes scroll together by default and `window.syncScroll` frees them, and *Remove Split* puts the window back to one view. **And since T11 there can be more than one window and the product arranges them**: tile horizontally or vertically, cascade, *Merge All Windows*, and a window list — all over this process's own windows with Qt geometry and no new `platform/` interface (D4-17), per monitor, and refused rather than inert with one window open. **`window.new` stopped meaning *an empty window* and became a second VIEW of the document in front**: its own `DocumentHandle`, because two windows each run the three-step close protocol, and **no second journal**, because two journals over one file would be two Saves racing for the same bytes — so it lays out from the first window's model tree at its revision and an unsaved rotation shows in both, and when the first closes the second detaches and goes on drawing the file. The two scroll together by default; `window.syncScroll` frees them, and it is one switch with two subjects (a split where there is one, the windows where there is not). **And the window that is not in front now queues its viewport at `Prefetch`** — A2's named work, built — so a second window can no longer take the single render worker away from the one a person is looking at. **And since T13 the product remembers the session**: a clean quit writes what was open — the documents and their tab order, the tab in front, each one's layout mode, zoom intent, page, scroll and guides, each window's split and panes, and the geometry keyed by the monitor arrangement it was on — and the next launch puts it back **third**, after the crash-recovery offer and after the command line's files, skipping any path those already opened so one file is never in two tabs. It never brings back an EDIT: a document closed with *Don't Save* comes back at the bytes on disk, because unsaved work belongs to the recovery journal and two mechanisms restoring one edit would restore it twice. `general/restoreSession` off writes nothing and restores nothing; `general/rememberWindowLayout` and `general/rememberPaneLayout` subtract the geometry and the panes. **And since T12 `Ctrl+3` finally means what `Features.md` §1.2 says it means**: Fit Visible fits — and scrolls to — the page's **content bounding box**, the union of its object bounds taken through the engine and cached per page, falling back to the CropBox on a page with no objects. That closes **Stage 2's I-28**, which had made `Ctrl+3` a second name for `Ctrl+0` on every document whose CropBox is its sheet. Beside it: `Ctrl+D` opens **Document Properties** — the Info dictionary, the version, the page count, the current page's size, the security and repair lines, the path and the size — which **edits nothing and constructs no command**; `Ctrl+L` is **reading mode**, the chrome gone and the window kept, restoring the panes, widths and dock sides it hid rather than a default layout; and the page box speaks **logical page labels**, so a reader on the page printed "1" sees "1" with the physical index beside it and can type `iv` or `A-12` where the document has one. **And since T7 the page surface has real tools**: Hand, Select, Marquee Zoom and a Loupe, armed stickily, with `Shift` held giving a temporary hand without disarming anything, `Esc` abandoning a drag and leaving the view exactly where it was, and auto-scroll at a signed speed that stops at the end of the document. **And since Stage 5 T5 a person can select and copy TEXT**: the engine thread extracts a page's characters with their boxes (`core/text/PageTextModel`, T2), `LayoutAnalyzer` orders them into lines, blocks, columns and tables by geometry (T4), and the Selection tool drags in that reading order — across pages and from one column to the next — selects a word, a line or a block on two, three or four clicks, a column on `Alt`+drag, and extends on `Shift`+click; `Ctrl+C` writes plain text, HTML and, for a column, a table in one copy, and `Ctrl+A` selects the whole document, its unread pages extracted in the background when it is copied. Glyphs whose font gives no Unicode are refused, not pasted. **And since T6 there is SEARCH below the UI**: `core/text/DocumentIndex` keeps every extracted page per document identity -- shared by a `window.new` companion, its own budget `search/indexCacheMB`, a rotation's `Scope` dropping the page -- and `core/text/SearchEngine` matches a query over it on workers of its own, every flag (whole words, case, diacritics, regex) through T4's one normalizer, never across a paragraph, with the bound said and unmapped glyphs reported; the selection reads its pages from the same index. The ten `search/*` keys have their Preferences page (nine of 26). **And since T7 a person can FIND**: `Ctrl+F` opens a find bar above the page -- one per tab -- that searches as you type (a 200 ms pause, the newer search cancelling the older), highlights every hit over the page and the current one deeper, steps with `F3`/`Shift+F3` and `Ctrl+G`/`Ctrl+Shift+G` and wraps saying so, takes the view to the current hit, toggles regex, whole words and case with `Alt+R`/`Alt+W`/`Alt+C` -- which ARE the settings -- searches the whole document, the selection or a page range, reports an invalid regular expression rather than *no matches*, says how many glyphs had no Unicode, and `Esc` closes it and clears every highlight. **Since T14 it never widens the window**: where the row is too narrow, the toggles, *Highlight all* and the scope fold behind a *More* button into a popup (S5 guide I-10 -- until then `Ctrl+F` widened a 900-pixel window to 1,102). **And since T8 the find bar searches beyond the page text**: comments, form fields, bookmarks and attachments, each only when its `search/include*` switch on Preferences' Search page is on -- read through ONE facade reader, `IDocumentBackend::searchUnits`, a part at a time (a page's annotations, the outline, the attachments) as an engine task on the render thread, once per document; a comment or field hit goes to its rectangle, a bookmark hit to its destination, and an attachment hit -- its name, its description, or the text of an attached PDF one level deep -- is selected and opens nothing, the status line naming the class. An attachment's `/Desc` is read first-party (`pdf/ops/EmbeddedFileReader`), because PDFium's attachment API cannot. **And since T9 there is ADVANCED SEARCH**: `Ctrl+Shift+F` opens a dock that searches a FOLDER, recursive or not, with every flag and content class -- each file opened in pieces, its engine work queued behind every visible tile and the rest on a helper thread, so the UI thread opens nothing -- never a tab and never a password (an encrypted file is listed as skipped); results stream in per file with snippets, sort by relevance (the file's hit count), page or file, filter by author, subject and date, refine, and export as CSV through the stage's one planned waiver; opening a result shows its hits in that file's tab. **And since T10 there is a SNAPSHOT**: `S` arms a tool whose dragged band is re-rendered -- not grabbed -- through the same scheduler, bypassing the tile cache, at `pageDisplay/snapshotDpi` (0 as displayed, 72 to 600 dpi, bounded at 50 megapixels and said when reduced), from the one page the band is mostly over, onto white, and put on the clipboard as an image with the status bar saying its size and resolution; none of the inversion, grid, guides, selection or highlights is in it, and `Esc` or a click copies nothing. **And since T11 there is REFLOW VIEW**: `Ctrl+4` replaces the page surface with a read-only text surface of the analyzer's blocks in reading order, re-wrapped to the window in Qt's fonts at the model's size, weight and italic -- a window of pages round the reader's place, grown as they scroll -- entering at the block at the top of the page and returning with the reflow's top block at the top of the page; the zoom chords set its text size, it claims the reading keys, it asks for no raster, and a find hit takes the tab back to the page. **And since T12 there is a COMPARER below the UI**: `core/text/DocumentComparer` aligns two documents' pages, diffs each pair's words (Myers, linear space, under a cap that reports unrelated pages *replaced*), finds moves, and at its *appearance* sensitivity compares 72 dpi rasters outside what the words claimed -- with the header/footer band, page numbers and whitespace each ignorable -- on a thread of its own (`ComparisonRun`). **And since T13 a person can COMPARE**: `Ctrl+Alt+C` opens a dialog for the older and the newer file -- open tabs or any path -- the sensitivity and the three ignore-lists, and a window of its own shows the two side by side, each over a handle of the comparison's own and neither through a journal, scrolled together by ALIGNED page so the page after an insertion sits beside its counterpart, or as an overlay in which ink both share is grey, ink only the older has red and ink only the newer has blue; the Differences pane lists every change in page order with a count by kind, and `Ctrl+]`/`Ctrl+[` step both views and the pane, wrapping with a message. Its bar's sensitivity and ignore switches ARE the four `documents/compare*` settings, and changing one re-runs the comparison over the text already read; every other key pressed in it is claimed, so nothing reaches the tab behind it. Its text is read through the index at `Background`, analyzed and diffed off the UI thread, and its rasters bypass the tile cache.

**What is deliberately absent, so it is not reported as a defect.** **The engine implements 4 of the
backend's 7 interfaces** -- `ISigner`, `IOcrEngine` and `IIndexer` are absent, and **the platform bundle 6
of its 18** (both re-derived 2026-10-02 at Stage 5 T14 from the startup log, which names each missing one).
`platform::IAcceleratedSurface`
is declared and unimplemented, and **Stage 4 T2 decided on 2026-09-19 that it stays that way** — the row
stops deferring and records a measurement (`Tech-Stack-Plan.md` §3.3, *A2 at Stage 4*): under the
workload this stage creates the viewport's p95 is **10.59–10.74 ms against a 16.667 ms budget**, and
the reason it is not built is structural rather than comfortable — a surface presents finished pixels
and does not rasterize them, while the cost measured is PDFium behind I-05's engine lock, where 1, 2, 4
and 8 workers give 13.199, 14.078, 14.093 and 13.808 ms. **T14 re-read the figure through the shipped
panes on 2026-09-23 and the verdict is final and plainly `go`** -- 7.03-8.56 ms under D4-16's
workload, and the two-window column that had withheld the plain verdict is 4.01-4.75 ms now that a
background window queues at `Prefetch`. Font substitution
runs through our own policy but has **no UI**: only the log names the face. `pageDisplay/lineWeight`
is a checkbox that does nothing — **I-06** and **I-11** (`Stage2-Rendering-Testing-Guide.md` §8) are
both open **by decision**, each a feature the engine or the licence cannot deliver, kept as a
declared-and-inert surface so there is a row to fill in the day the constraint lifts. Four §8 Viewer chords -- `Ctrl+B`, `Ctrl+E`, `Ctrl+.` and `Ctrl+,` (re-counted 2026-10-02, Stage 5 T13, by `tst_viewercommands`' census; there were thirteen at Stage 3's sign-off, Stage 4 T8 moved three across, Stage 4 T12 moved `Ctrl+L`, Stage 5 T10 moved `S`, Stage 5 T11 moved `Ctrl+4` and Stage 5 T13 moved `Ctrl+Alt+C`, `Ctrl+]` and `Ctrl+[`) -- stay `NotFound` and deliberately leave the key unconsumed. **A page colour can be INVERTED but not SUBSTITUTED** — `view.pageBackground` is declared and unregistered because no declared settings key holds a colour pair (guide **I-07**, deferred as `DW-1` in `claude-plan/Deferred-Work.md` — no stage's plan carries it yet). **Since Stage 5 T5 the Selection tool selects and `Ctrl+C` copies** — plain text, HTML and a table — and glyphs with no Unicode text are refused rather than pasted.
**263 of 374 declared commands are unregistered** (re-derived 2026-10-03 with `--list-actions` after the rulers' corner box became a button, on the user's request, which minted AND registered the chordless `view.cycleRulerUnit` -- so the total and the registered count moved, to 374 and 111, and the unregistered did not; 373 before it, re-derived 2026-10-02 at Stage 5 T14 and **unmoved by T14**, which registered and minted nothing -- the find bar's *More* is part of the bar, not a command; T13 registered `compare.documents`, `nav.nextDifference` and `nav.prevDifference` and minted nothing, and left `compare.showReport`, a report, to Stage 8; 266 at T12, which registered nothing; the same at T11, which registered `view.reflow` and minted nothing; 267 at T10, which registered `tool.snapshot` and minted nothing; 268 at T9, which registered `find.advancedSearch` and minted nothing; 269 at T8, which registered and minted nothing; the same at T7, which registered the eight Find / Search commands and minted nothing; 277 at T5, which registered `edit.copy` and `edit.selectAll`; 279 before it, re-derived 2026-09-23 at Stage 4 **T13** and **unmoved by it** — session restore is not a command and has no chord, so T13 registers nothing and mints nothing, which is the second task of this stage to move neither number; **the total moved from
361 to 363 at T7, to 366 at T8, to 370 at T9, to 371 at T10 and to 373 at T11, and did NOT move at T12**, which is the first task of
this stage to register something without minting anything: the twelve ids this stage mints are `tool.loupe` and `view.autoScroll`, then
`view.lockGuide`, `view.deleteGuide` and `view.clearGuides`, then `window.splitVertical`, `window.spreadsheetSplit`,
`window.removeSplit` and `window.syncScroll`, then `present.start`, then `window.merge` and `window.list`, all chordless, while T12's two —
`view.readingMode` and `file.documentProperties` — were declared at Stage 1 with their chords and registered by nothing until then), which is the honest
state rather than a shortcut — including the nine §8 Page Thumbnails rows and the eight §8 Bookmarks rows that
each need a `Command`, the seven `Ctrl+Shift+*` panes whose models do not exist, and **`edit.cancel`, left
unregistered on purpose** (guide I-06: a `Common` row, and a DISABLED registration consumes a key just as a working
one does).

**Two verdicts worth knowing before quoting a number.** **A2 = `go` on both of D2-1's measurements**
— probe p95 **0.532 ms** of engine service time, product p95 **13.03–13.20 ms** of queued latency against
a 16.667 ms frame budget, **about 1.27× headroom** (re-measured 2026-09-14 behind a latency control;
T18's 14.18 ms and 1.18× were an upper bound, Stage 2 **I-41**). Those are different quantities and the pair is the
answer; quoting the probe's *31× headroom* as the product's is the exact error D2-1 exists to
prevent. And the **golden suite's green is a narrower claim than it looks**: *the product rasterizes
our own generated corpus the same way it did at capture* — never *correctly*, and nothing at all
about real-world PDFs (`golden/README.md` owns that wording, and every place that quotes it repeats
the bound).

**PDFium cannot be called from two threads at once**, measured at Stage 2 T3: eight workers, each
with its own `FPDF_DOCUMENT`, corrupt the heap, because what PDFium shares is process-global rather
than per-document. **T11 settled it** — one render thread with a queue, `workerCount` defaulting to
**1** — on the measurement that eight workers buy 5 % while the engine is already far inside the
frame budget. Reproduce with `rawpage_bench_render.exe 8 --unsafe-parallel`; it is *expected* to
crash. **I-05 is closed**; the engine lock in `PdfiumInit` stays — and **since Stage 5 T3 it is a
first-come, first-served ticket lock that records its owner**: `std::mutex` let a thread taking it in
pieces starve the render worker (S5 guide I-02), and every engine call — at open too, which S5 guide
**I-01** found unlocked since Stage 4 T4 — is made under it, counted by `requireEngineLock()`.

## The document set — who owns which facts

Each document is the single source of truth for its own subject; the others cross-reference rather
than restate. **When something changes, fix the owning document; never re-decide a fact locally.**

| Document | Owns |
|---|---|
| `claude-plan/Features.md` | The capability catalog and the **authoritative key-binding tables** (§8). No stack choices. |
| `claude-plan/Tech-Stack-Plan.md` | Locked stack, license posture, directory layout, the inward-only dependency rule, mutation-through-commands, the first milestone. |
| `claude-plan/Architecture-FileMap.md` | What every planned file does, plus the call-trace flows A–K. |
| `claude-plan/Implementation-Plan.md` | Stage ordering, **live status**, shared conventions, testing strategy, Definition of Done, traceability. |
| `claude-plan/Stage<N>-<Area>-Plan.md` | *Optional per stage.* One stage's task-level execution order, its stage-local design decisions (`D<N>-n`) and its blocking preconditions. **Six exist:** `Stage0-Toolchain-Plan.md`, `Stage1-AppShell-Plan.md`, `Stage2-Rendering-Plan.md`, `Stage3-Mutation-Plan.md`, `Stage4-ViewingSurface-Plan.md` and `Stage5-TextSearch-Plan.md` (2026-09-29, its §0 answered the same day), each written before that stage's code. **A stage plan owns no product facts and no live status** — those stay in the four documents above. |
| `claude-plan/Stage<N>-<Area>-Testing-Guide.md` | How one stage is verified — **both** the automated pass I run and the manual pass you walk, plus that stage's **issue log** (§8). **Six exist** — one per stage built, plus **Stage 5's, written 2026-09-29** with its plan and before its code (115 automated, 9 manual; its status line says which cases have results -- every task's, T1 to T14, all 114 ticked as of 2026-10-02, and A-115, the rulers' corner box, added and ticked 2026-10-03; M-01 and M-02 walked and passed, the other seven not) — and Stage 4's, written from its plan on 2026-09-18 **before any Stage 4 code** and now **fully run, walked and signed off (2026-09-27)** — **108** automated (79 written, T11's A-66(a), A-80 added 2026-09-24 with I-10's fix, A-81 to A-86 added 2026-09-26 with I-11's to I-15's fixes and decision D4-18, A-87 and A-88 the same day with I-16's and I-17's, A-89 with the user's dotted-guide-preview request, A-90 and A-91 on 2026-09-27 with I-18's and I-19's fixes, A-92 the same day with I-21's, and A-93 with I-22's guard, and A-94 with I-23's fix, and A-95 and A-96 with I-24's and I-25's, and A-97 and A-98 with I-26's and I-27's, and A-99 and A-100 with decision D4-20, and A-101 and A-102 with D4-22, and A-103 and A-104 after the walk with I-28's and I-29's fixes, and A-105 to A-107 with I-30's and I-31's), 12 manual, **all 108 carrying a result and ALL TWELVE manual cases walked and signed off — M-11, 2026-09-24, passed; M-01, 2026-09-26, failed its first walk and passed its re-walk; M-02, M-06, M-03, M-04 and M-07, 2026-09-26, passed; M-05, 2026-09-26, failed at step 5 and passed its re-walk; M-08, 2026-09-27, failed its first walk at step 1 and its first re-walk of that step, and passed its second; M-09, 2026-09-27, failed its first walk at the cascade and passed its re-walk the same day; M-10, 2026-09-27, stopped at step 3 of its first walk and passed its resumed walk the same day; M-12, 2026-09-27, passed its Loupe re-walk after I-26 and I-27** (re-derived 2026-09-27: 108 case headers, none still boxed — A-04, a `portable-asan` job this machine cannot run, was read from Linux CI's log); **thirty-one** issues, **only I-07 open (S4); I-30 (S3) and I-31 (S1) — the kill harness's instants locked to the timer tick, and a save killed inside `ReplaceFileW` leaving no file at the document's path — were found by a clean-rebuild run after the walk and closed that day; I-28 (S3) and I-29 (S2) — test cases leaving process-wide state behind, and a second window compounding the UI font scale — were found after the walk by investigating why the UI binary could not run whole, and closed that day; I-26 (S3) and I-27 (S3) — a loupe drag pinning the lens, and the temporary hand leaving it on screen — were raised by M-12's walk; I-24 (S2) and I-25 (S3) — a re-docked sidebar left out of the window, and a closed sidebar's strip that opened nothing — were raised by M-10's walk; I-23 (S2) — an arranged window's title bar off the screen — was raised by M-09's walk; I-10 (S2) was raised by M-11's walk, I-11 (S2) and I-12 (S3) by M-01's, I-13 (S3) by the user's question after it, I-14 (S2) by M-02's walk, I-15 (S2) by M-05's and I-16 (S2) — a guide snap no gesture called — by reading the code before M-07's walk and I-17 (S3) — no cursor readout anywhere — by the user preparing it, and I-18 (S2) and I-19 (S2) — a spreadsheet split's zoom reaching only the body, and frozen panes scrolling on their own — by M-08's first walk, and I-20 (S2) and I-21 (S3) — a defect in I-19's fix, and the keypad's `+` — by its re-walk, and I-22 (S3), four document paths holding control bytes, by a byte-level read, each closed the day it was raised**, with **I-05 closed at T12 and I-01 and I-09 at T13**; and its verdict PRODUCED at T2 — A2 at Stage 4 is `go, with named custom work`, which answered `platform::IAcceleratedSurface`: not built: `Stage0-Toolchain-Testing-Guide.md` (run, signed off, closed), `Stage1-AppShell-Testing-Guide.md` (fully run and fully walked, issues I-01…I-55, none open), `Stage2-Rendering-Testing-Guide.md` (automated half run; manual half **fully walked and signed off**, the last case on 2026-09-14; every §9 item met, and the stage signed off by the user, on 2026-09-14), `Stage3-Mutation-Testing-Guide.md` (**every case run and recorded, every manual case walked and signed off, and the stage signed off by the user on 2026-09-18**; sixteen issues, none open) and `Stage4-ViewingSurface-Testing-Guide.md` (**written before the code, RUN, and the stage signed off by the user on 2026-09-27 — T1 to T14 recorded, all 108 cases carrying a result (A-04 read from Linux CI's log, A-85 and A-94 from `windows`-platform runs), ALL TWELVE manual cases walked and signed off — M-11, 2026-09-24, passed, M-01, 2026-09-26, passed on its re-walk, M-02, M-06, M-03, M-04 and M-07, 2026-09-26, passed, and M-05, 2026-09-26, passed on its step-5 re-walk, and M-08, 2026-09-27, FAILED its first walk at step 1 and its first re-walk of that step, and passed its second, and M-09, 2026-09-27, FAILED its first walk at the cascade and passed its re-walk, and M-10, 2026-09-27, stopped at step 3 and passed its resumed walk, and M-12, 2026-09-27, passed its Loupe re-walk; thirty-one issues, one open — I-07 (S4)**). **Each guide owns its own case counts, results and issue states — read them there rather than from a figure quoted elsewhere.** Every guide has its own `A-nn`/`M-nn`/`I-nn` namespace, so a bare `I-34` means Stage 1's. Some cases are Linux jobs this machine cannot run (sanitizers, fuzzing) and are recorded as such rather than as pending. Each is written right after its stage's plan and **before that stage's code**, per the workflow section below. Do not write one for a stage that is not next; do not treat its absence as a gap. |
| `claude-plan/Deferred-Work.md` | **Work a stage deliberately did not do and that no later stage's plan has taken in** (`DW-n`, created 2026-09-27): each item's ownership state, what exists today and what is absent. Not the capability (`Features.md`), not the finding (the raising guide's §8), not the repair. **When a stage's plan takes an item, record it there and mark the entry taken — never delete a row.** One entry so far: `DW-1`, substitute page colours (S4 guide I-07). |

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
| `O1`…`O11` | Open decisions carried forward | `Implementation-Plan.md` §8 |
| Flows `A`…`K` | Call traces | `Architecture-FileMap.md` §5 |
| `D0-n` / `D1-n` | Stage-local design decisions | that stage's `Stage<N>-*-Plan.md` §3 |
| `T1`…`T15` | Stage tasks | that stage's `Stage<N>-*-Plan.md` §6 |
| `V-n` (S0) / `P-n` (S1) | Blocking preconditions, yours | that stage's `Stage<N>-*-Plan.md` §7 |
| `R-1`…`R-9` | The A3 ribbon acceptance criteria | `Stage1-AppShell-Plan.md` §5.3 |
| `A-nn` | Automated test cases | that stage's `Stage<N>-*-Testing-Guide.md` §5 |
| `M-nn` | Manual test cases | that stage's `Stage<N>-*-Testing-Guide.md` §6 |
| `DW-n` | Deferred work no stage has taken | `Deferred-Work.md` |
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

### Where does this fact go? — the routing table

The table above says what each document **owns**. This is the inverse lookup, which is the one
actually needed mid-task: *I have just learned, decided or measured something — where does it get
written?* **Find the row for the kind of fact, write it there, and cross-reference from anywhere
else that needs it.** If a fact seems to belong in two rows, it belongs in the more specific one and
the other cites it.

| The kind of fact you have | Goes in | Not in |
|---|---|---|
| A **capability** the product should have; what a feature does for a user | `Features.md`, in the matching §1–§10 catalog section | a stage plan — a stage *implements* a capability, it does not define one |
| A **key binding** or chord | `Features.md` **§8**, the authoritative tables | anywhere else; every other mention cites §8 |
| A feature **removed from scope** | `Features.md` → *Removed from scope* — the single record | an issue log; removal is not a defect |
| A **stack, library or licence** choice; the directory layout; the dependency rule | `Tech-Stack-Plan.md` §2 (decisions `D1`…`D9`), §6 (dependencies), §8 (licensing) | CLAUDE.md, which summarizes the locked set and owns none of it |
| A **go/no-go verdict** or assumption (`A1`…`A7`), with its measured figure | `Tech-Stack-Plan.md` **§3.3** | a stage status block — quote it there, own it here |
| A **project-level risk** carried across stages (`R1`…`R8`) | `Tech-Stack-Plan.md` **§10** | a stage plan |
| **What a file does**, and its ○/🟡/✅/⤵ status | `Architecture-FileMap.md` §3, as that file's row | anywhere else; a planned file with no row is the gap |
| A **call trace** across layers (flows `A`…`K`) | `Architecture-FileMap.md` **§5** | prose in a stage plan |
| **Stage ordering**, and **live per-task status and deviations** | `Implementation-Plan.md` **§4**, that stage's status block | CLAUDE.md — it points at these blocks and must not copy them |
| A **convention that binds all later work** | `Implementation-Plan.md` **§1.5**, appended and never renumbered | a stage plan, where a cross-cutting rule would be invisible to later stages |
| The **testing strategy**, and why a test target is separate | `Implementation-Plan.md` **§2** and **§2.1** | a testing guide, which covers one stage only |
| Work a stage **defers** that **no later stage's plan names** | `Deferred-Work.md`, as a `DW-n` entry — and the raising guide's §8 points at it | a sentence in the deferring stage's guide naming an owner whose plan never heard of it |
| An **open decision** nobody has settled (`O1`…`O11`) | `Implementation-Plan.md` **§8** — and record the resolution there when work forces one | an inline choice in the code that nothing records |
| One stage's **task order** and **stage-local design decisions** (`D<N>-n`) | that stage's `Stage<N>-*-Plan.md` §3 and §6 | `Implementation-Plan.md`, which owns ordering *between* stages |
| A **blocking precondition** that is the user's to clear (`P-n` / `V-n`) | that stage's `Stage<N>-*-Plan.md` §7 | a testing guide |
| A **test case**, automated (`A-nn`) or manual (`M-nn`) | that stage's `Stage<N>-*-Testing-Guide.md` §5 / §6 | the plan; the plan says what is built, the guide says how we know |
| A **defect, its mechanism, and why the suite missed it** (`I-nn`) | that stage's `Stage<N>-*-Testing-Guide.md` **§8** | a commit message alone — the log outlives the fix |
| **Design reasoning about one file** — why this class, this order, this trap | that file's **own header comment** | a plan document, which no one reads while editing the file |
| What a **golden baseline** claims, and why one was regenerated | `golden/README.md`, `golden/manifest.json`, `golden/UPDATES.md` | a stage plan |
| Where a **corpus file** came from and what fault it carries | `corpus/PROVENANCE.md`, one row per file | a test case comment |
| A **toolchain or engine pin** assertion (`TA-n`, `PA-n`) | the `cmake/*.cmake` file that enforces it | prose anywhere; the assertion is the record |
| **How to run something**, and the traps in running it | `CLAUDE.md` → *Commands* | a stage plan |

**What belongs in `CLAUDE.md` itself is narrow**: what binds every session regardless of task — the
locked stack in summary, the architecture, the machine-enforced gates, the commands, the convention
index, and this routing. **Per-task history, per-file description and live status have owners above
and must not be copied here.** When in doubt, this file gets the pointer and the owner gets the fact.

### How to write it, once you know where it goes

Each rule below cost this project a defect or a wrong decision; the incident is named.

1. **Fix the owning document; never re-decide a fact locally.** A second copy is a future
   contradiction — one gets updated and nothing says which. *(This file carried a 749-line duplicate
   of `Implementation-Plan.md` §4 until 2026-09-06; the copy was already the staler one.)*
2. **Re-measure rather than remember.** Every count — tests, cases, files, includes, waivers,
   catalog keys — is re-derived from the tool or the tree in the same change that quotes it. Prefer
   naming the command over pasting its output. *(The catalog, target, waiver and include censuses
   have each drifted.)*
3. **A figure from a named run is history, not status.** *"Run 32785493072: 334/334"*, never *"the
   suite is 334"*.
4. **Do not write a forward-looking sentence that nothing re-derives.** *"Next action: T15"*,
   *"blocked on golden baselines"*, *"the mechanism already exists"* — each reads as settled long
   after it stops being true. **A deferral's note about how it will be fixed is a guess by someone
   who did not build it**: record what is absent and who owns it, not the repair. *(Stage 2 I-10;
   T17 found three such claims stale at once.)*
5. **State what a green result actually claims, wherever it is quoted** — *"rasterizes our own
   generated corpus the same way it did at capture"*, never *"rendering is correct"*. *(D2-11.)*
6. **Record the measurement and the reasoning, not just the conclusion.** *"`workerCount` is 1"* is
   unactionable; *"1, because eight workers buy 5 % while p95 does not improve"* survives the next person
   who wants to raise it.
7. **Verify a doc change by reading the thing it was meant to change** — the rendered diff, the
   regenerated file, the tool's output — never by re-reading your own edit. *(Stage 2 I-16: a
   `.gitattributes` fix whose own comment claimed in the present tense that it worked never had, for
   four days.)*
8. **Cite every label with its home on first use** (see above), and **correct a stale claim in place**
   rather than appending a newer sentence beneath it.
9. **Documentation of a message names its catalog key, never the English string** — `core/` holds no
   user-facing English. *(Convention 5.)*
10. **Never put a backslash through a shell here-document.** The agent's Bash tool collapses `\\`
    to `\` on the way in, so a script that escaped a Windows path correctly writes BACKSPACE, CARRIAGE
    RETURN, BELL or VERTICAL TAB into the file, and a terminal shows nothing wrong. Edit with the
    Edit/Write tools, or build the backslash as `chr(92)` in a script written with Write.
    `gates.text_bytes.*` refuses the result. *(Stage 4 guide I-22: four paths, five weeks.)*

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
  libFuzzer, so the `portable-asan` / `portable-fuzz` presets build `core/`, `pdf/cos/` and — since Stage 3 T3 (D3-3) —
  `pdf/ops/`, the layers that are portable by rule, on Linux instead. A finding there is reproduced in `x64-debug`
  before it counts as fixed. Debug info is DWARF, not PDB: crash dumps symbolize against archived
  unstripped binaries.
- **Async tiled rendering** at physical DPI on a worker pool; the UI thread never parses or rasterizes.

## Architecture (once code exists)

Layers are separate static libraries (`rawpage_core`, `rawpage_cos`, `rawpage_ops` — portable, from Stage 3 T3 — `rawpage_pdfium` — from Stage 2 T2 — `rawpage_platform`, `rawpage_ui`) linked
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
- `platform/win/` — the only OS-specific code; implements the 18 `platform/platform.h` interfaces (six of them so far — the product logs its own census at startup, and a missing one is a null member, convention 6)
  and makes no policy decisions.
- `i18n/`, `res/`, `corpus/`, `golden/` are **data, not code**.

### Mutation-through-commands — the load-bearing design

Every document change is a `Command` (`name/scope/apply/revert/serialize`) run through
`CommandJournal`. `apply()` expresses **intent** to a `MutationSink` interface (`setPageRotation(3, 90)`)
— it never touches bytes; `pdf/ops/` decides how intent becomes COS objects. Read flow D in
`Architecture-FileMap.md` §5 before writing any mutating code; every mutation in the product, from a
highlight to Write-mode typing, has exactly that shape.

Consequences that fall out of it: undo/redo, an fsync-per-command recovery journal
(`%LOCALAPPDATA%\RawPage\recovery\<docid>-<pid>-<n>\`, one flushed line per operation — Stage 3 T9), precise `Scope`-keyed invalidation, and incremental save
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

These are machine-enforced by CI gates, not by review discipline. **Four are specified, and all four
are live** — the fourth, `mutation-lint`, from Stage 3 T6. `checks` runs four and says four; a count it
names that disagrees with the commands it runs is the bug.

| Gate | State | Fails on |
|---|---|---|
| `tools/layer-lint` | ✅ **live** | an outward include; **`FPDF_` outside `pdf/render/`** — narrowed from `pdf/` at S2 T2 (D2-9), because `pdf/cos/` is first-party by rule and is half of what the portable presets build on Linux — and, as a companion rule, **a PDFium HANDLE named outside `pdf/render/`**, which catches `ScopedFPDFDocument` and the deleter family that contain no underscore after `FPDF` and slip past the first rule entirely; a filesystem write outside `pdf/`, `core/settings/`, `tools/` and `tests/` — the last two because a gate and a test fixture write by definition; a path read out of `argv` outside `app/main.cpp`; **a `QAction` constructed outside `ui/shell/ActionFactory.cpp`** (and `RawPageAction.h`, which is the type the factory returns) (D1-7, four spellings, non-escapable); **an OS header in `platform/platform.h`** and **an engine or QtGui include in `core/backend.h`**, each its own layer entry because those files are included by three or four layers apiece |
| `tools/license-audit` | ✅ **live** | any dependency SPDX identifier outside the allowlist — **and from S2 · T7 it audits the RESOLVED CLOSURE**, enumerating every `share/*/vcpkg.spdx.json` in the installed tree rather than only what `vcpkg.json` declares (issue I-02). An installed tree *is* the closure, so this reads what we ship instead of keeping a second hand-maintained list; with **no** installed tree the enumeration is inert and the map still enforces the rule, which is what keeps the CI `gates` job honest. Two fixtures with a control prove it fires and does not simply fail on every tree |
| `tools/i18n-lint` | ✅ **live** | a literal reaching a user-facing sink and not wrapped in `tr()` (escapable, `// i18n-lint: allow-literal(reason)`); a key missing from a shipped locale; an `ActionId` with no `action.*` catalog entry. `orphan-key` is S3 — reported, never blocking, and a quoted key reference **ending in a dot** is a namespace reference covering every key beneath it, because several catalog families are built from a table at runtime and appear whole in no source (T14, issue I-27). **An action's string id is not a key** — on an `ActionId.def` row only the label column is, and a ribbon `"action"` names a command (S5 guide I-08, when the `compare.` family met the action `compare.documents`). **`wrong-context`** (S5 T7, blocking): a catalog entry outside the `<context>` its key's first segment names is compiled and found by nothing, so the product shows the key (S5 guide I-05). Stage 1 ships `en` and `zh-TW` only; `zh-CN` and `ja` are declared `"shipped": false` (D1-14) |
| `tools/mutation-lint` | ✅ **live** (S3 · T6) | a class deriving from `Command` under `core/cmd/commands/` with **no `core/cmd/Commands.def` row**, **no Catch2 test case tagged `[serialize:Class]`** or **no test case tagged `[revert:Class]`** under `tests/` (`tests/gates/` excluded); and a table row whose header column does not declare its class — a column the compiler never reads. Read as text with the standard library only (D3-12). **It proves a test exists, not that it tests anything**: a revert that lies is `tst_reversibility`'s to catch (Stage 3 guide A-44), and `--explain` lists what the gate cannot see |

Two rules are escapable, both with an inline marker **on the same line as the match** — not the
line above — and both censused by that gate's `--explain`:
`// layer-lint: allow-filesystem-write(reason)` and `// i18n-lint: allow-literal(reason)`.
**Fifteen filesystem-write waivers are live and no literal waiver is** (re-derived at Stage 5 T14, unmoved since T9) —
three in `app/CrashHandler.cpp`, two in `app/LogFile.cpp`, two in `core/render/DiskTileStore.cpp`
(one write and one delete, because every deletion in that file is funnelled through a single helper
to keep the census at two), one on the named-pipe `CreateFileW` in
`platform/win/WinSingleInstanceBus.cpp`, four in `platform/win/WinDurableFiles.cpp` — one per
call spelling: `CreateFileW`, `MoveFileExW`, `DeleteFileW` and, since S4 guide I-31 replaced
`ReplaceFileW` with one atomic rename, `SetFileInformationByHandle` — and two in
`core/cmd/RecoveryJournal.cpp`, the journal's append and its whole-file write, and since Stage 5 T9 the one
planned in D5-13: `ui/panes/SearchPane.cpp`'s CSV export of the Advanced Search list. **Run `--explain` rather than trusting that list**: it
is the census, and it distinguishes a waiver that **silenced something** from one that did not,
which is Stage 0's I-20 written into the tool rather than remembered. A waiver count creeping
from a handful to a hundred is what that census exists to make visible.

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

## Conventions discovered while building — these bind all later work

**Twenty-four of them, and the full text with the measurement behind each is
[`Implementation-Plan.md` §1.5](claude-plan/Implementation-Plan.md).** Most were written the day a
defect got past a green test suite, so the reasoning is the load-bearing part and it lives there
rather than here. **The numbers are stable and are cited across the document set** — several guides
say *"convention 19 (`CLAUDE.md`)"* — so append to this list, never renumber it. The titles, so you
know when to go and read one:

1. **Nothing may read a path out of `argv`.**
2. **Catch2 test names are ASCII — and contain no comma.**
3. **`core/` may not name a Qt GUI type.**
4. **`settings.ini` is read through our own codecs, never through `QVariant`'s conversions.**
5. **The catalog key is the `<source>`, and `en` is a real catalog rather than the identity.**
6. **An unimplemented `platform/` interface is a null member, never a stub.**
7. **Every negative control must actually exercise the thing.**
8. **A gate reads the files the compiler reads, `.def` tables included.**
9. **`ui/` looks outward to exactly two files, and each is constrained by its own layer entry.**
10. **A widget that connects a child's signal to a lambda holding `this` must disconnect in its destructor.**
11. **`core/` may not let the *host* decide a domain rule.**
12. **A widget test must drive layout itself, and `resize()` alone is not driving it.**
13. **A self-check meets its real subject once, and that is when it gets corrected.**
14. **A command handler may not capture the window that registered it.**
15. **A chord never becomes a `QAction` shortcut or a `QShortcut`.**
16. **A command that needs a user is DISABLED where there is none — it does not block.**
17. **A surface generated from a declarative table reads the TABLE, not a runtime description of it.**
18. **`core/Log.h` has ONE sink and `setLogSink` REPLACES it — so nothing chains it.**
19. **A test that reaches a behaviour through a back door must say what it therefore does NOT cover.**
20. **A colour is judged in BOTH themes, or it is judged in neither.**
21. **A palette is not a theme: the native style paints from the COLOUR SCHEME, and it wins.**
22. **A case proving a mechanism FIRES is half of proving it fires WHEN IT SHOULD — and the criterion will not ask for the other half.**
23. **A button that shows a command is an `ActionToolButton`, never a stock `QToolButton` with a default action — and anything retained per document is measured as LIVE heap, never as private bytes.**
24. **`core/`, `pdf/cos/` and `pdf/ops/` may use only Qt APIs the DISTRO Qt has** — CI builds them against Ubuntu's Qt **6.4**, not the pinned 6.11.1, and the Windows suite cannot catch the difference.

**Three are cited often enough to state in full here**, because they are the ones a new change
breaks:

- **7 · Every negative control must actually exercise the thing.** A control that does not fail for
  the right reason is worse than none. Two of Stage 1 T3's probes appended rows *after* an `#undef`
  and proved nothing.
- **19 · A test that reaches a behaviour through a back door must name what it therefore does NOT
  cover** — at the assertion, so the next reader subtracts it instead of adding it. Re-dock was
  green in CI and unreachable by hand for six days (**I-34**, `Stage1-AppShell-Testing-Guide.md` §8).
- **22 · A case proving a mechanism FIRES is half of proving it fires WHEN IT SHOULD**, and the
  criterion will not ask for the other half. Stage 2 T16's *control* — not its case — caught the
  product substituting Segoe UI for Helvetica on every page of every document (**I-13**).

### Accessibility is OUT OF SCOPE (2026-08-23)

Removed from the product entirely on the user's instruction — **not deferred**. The single record is
[`Features.md` → *Removed from scope*](claude-plan/Features.md); every other document
cross-references it. Gone: screen-reader names/roles, high-contrast deference (**D1-15** withdrawn),
Read Aloud with `platform::ISpeech`, the accessibility checker, alt text, and **PDF/UA**. Criterion
**R-8**, manual case **M-08** and precondition **P-2** were withdrawn with it.

**Deliberately kept, and not to be removed as collateral:** full keyboard operability and focus
visibility (core architecture, and a power-user feature); `core/doc/TagTree` and
`core/text/LayoutAnalyzer` (Write mode's structure under decision **O2** — Stage 13 infrastructure
that also happened to serve accessibility, not the reverse); Reflow view; and the ISO 32000
accessibility *permission bit*, which any conforming reader must round-trip.

### What the manual passes keep teaching

Stage 0's three S1s all came from manual cases no automated run could reach, and Stage 1 repeated the
lesson from both directions: the user's **M-01** walk found `Tab` visiting the ribbon groups in
collapse-priority order rather than visual order — invisible to a green offscreen run — and that
finding was then *automated*. **Write the manual half for what the harness structurally cannot see;
when a manual finding turns out to be measurable, move it.** Every manual case in this project was
walked and signed off by the user: **I do not mark a manual case.**

**A manual walk works inside `tests/`, and nowhere else.** Every `M-nn` case's working folder is
`tests\manual\<case>` under the repository root
(`D:\Users\squirrel\Documents\SideProject\RawPage\tests` on this machine) — the copy of the
corpus file being edited, what the product saves over it, and anything read back afterwards. Not the
Desktop, not `Documents`, not `%TEMP%`: a walk that scatters its files leaves nothing for the next
person to re-read when a result is disputed, and a walk must never edit the corpus file itself, which
is generated and checked byte-for-byte. `tests\manual\.gitignore` keeps the artifacts out of the
history while the folder stays tracked. **This binds the manual half only** — CTest's working
directory and the scratch directories tests create for themselves are unchanged.

`claude-plan/skills-specs-generation/` is a **vendored third-party skill set** (its own nested git
repo, MIT-ish LICENSE) that generated these docs. Don't edit it as part of product work.

## Commands

**`cmake`, `ninja` and `g++` are not on this machine's `PATH`.** Dot-source the environment first;
a command that works only *without* it is itself a finding.

```powershell
. .\scripts\env.ps1
cmake --preset x64-release
cmake --build --preset x64-release --target checks    # the live gates; CI runs this BEFORE compiling
cmake --build --preset x64-release
ctest --preset x64-release --output-on-failure        # 1399/1399 as of 2026-10-03 (S5 guide A-115), 4 skipped
```

The corpus is data, but it is **checked** like code — and the checks need Python, which is a
verification dependency and deliberately **not** a build one (`cmake --build` never calls it). They
run as `gates.corpus.*` in the `gates` preset; by hand:

```powershell
python corpus/make-corpus.py                     # regenerate all 97 PDFs and 24 JSON sidecars (S5 T1 added 27 PDFs)
python corpus/make-corpus.py --verify            # they regenerate BYTE-IDENTICALLY (D2-11, S2 T4)
python corpus/make-corpus.py --check-provenance  # every file has a PROVENANCE.md row, and vice versa
python corpus/make-corpus.py --self-test         # the negative control: each detection must fire
python corpus/make-corpus.py --check-faults      # S5: each text fixture carries what its row claims (A-14)
python corpus/make-compare.py --self-test        # S5: every comparison record re-derives its pair (A-15)
```

`make-corpus.py` with no flag **rewrites every file**, and a corpus PDF held open by a viewer makes
it stop with `PermissionError` part-way (measured at Stage 3 T1). Run a single `make-*.py`, or close
the viewer; `--verify` only reads, and is what says whether anything needed writing.

`corpus/signed/` is judged by OpenSSL, never by our reader (D3-2, `Stage3-Mutation-Plan.md` §3), with
one byte flipped in each signed range **first** — a control that verifies stops the run. The same
script is Stage 3 T2's A4 probe instrument (guide case A-28), so it takes any signed file:

```powershell
cmake -P tests/gates/ResolveOpenSSL.cmake                                  # which OpenSSL: path, how found, version
cmake -P tests/gates/CheckSignatures.cmake                                 # A-15 as CI's log step runs it, output shown
python tests/gates/check-signatures.py --openssl <exe> <signed.pdf>...     # the same over any files
ctest --preset x64-release -R a4_probe -V                                  # A-28: the writer writes, then OpenSSL
                                                                           #   judges; outputs in build\x64-release\a4-probe
python tests/gates/check-signatures.py --shapes corpus/signed/*.pdf        # A-16: the structure PROVENANCE.md claims
```

Diagnostics the entry point answers (T1), all usable under `QT_QPA_PLATFORM=offscreen`:

```powershell
.\build\x64-release\bin\rawpage.exe --version              # name, build id, Qt version
.\build\x64-release\bin\rawpage.exe --print-dpi-awareness  # expect PerMonitorAwareV2
.\build\x64-release\bin\rawpage.exe --print-args <paths>   # the D1-3 argv-vs-Qt measurement
.\build\x64-debug\bin\rawpage.exe   --crash-test           # debug only; writes a minidump
.\scripts\symbolize.ps1 -Dump <path.dmp>                   # resolves the faulting frame
.\build\x64-release\bin\rawpage.exe --dump-ui-strings     # every shell key + action label, resolved
.\build\x64-release\bin\rawpage.exe --list-actions        # 374 declared / 111 registered / 263 not (2026-10-03), + effective chords (A-72)
.\build\x64-release\bin\rawpage.exe --print-screen-metrics # the QScreen-to-MonitorInfo join, and which source won (I-20)
.\build\x64-release\bin\rawpage.exe --lang zh-TW --dump-ui-strings   # guide case A-72
.\build\x64-release\bin\rawpage.exe --help                # the parser's own vocabulary; exit 2 on a usage error
```

The four gates can also be run one at a time. `layer-lint` and `i18n-lint` have an `--explain` that prints their rules and
its live waivers — that census is how anyone notices waivers creeping from a handful to a hundred — and
`mutation-lint`'s prints its rules, what it cannot see, and every command with its row and tagged tests:

The golden baselines are data, and like the corpus they are **checked** like code — but unlike the
corpus they are **produced** by a tool rather than by a generator, and that tool refuses to run
without a stated reason (D2-3). `golden/README.md` owns the details; the four modes are:

```powershell
.\build\x64-release\bin\golden-update.exe --explain          # thresholds, census, paths, the machine-dependent cases
.\build\x64-release\bin\golden-update.exe --check-manifest    # every corpus file is a case or an excluded row, both ways
.\build\x64-release\bin\golden-update.exe --survey            # open every corpus file, print a measured manifest skeleton
.\build\x64-release\bin\golden-update.exe --reason "why the baselines moved"   # WITHOUT THIS IT REFUSES, exit 2
```

**Regenerating baselines is not a routine step and must never be the fix for a red suite.** A golden
baseline is a claim about one rasterizer; re-capturing it from the code that changed it turns a
rendering regression into a new reference and the diff then shows nothing but binary files. Read the
diff image `<build>/golden-failures/` holds first.

Two checks that are neither a `ctest` case nor a gate, and each has a recorded reason for not being one:

```powershell
.\build\x64-release\bin\rawpage_bench_ribbon.exe   # A-85 / R-9, the one A3 criterion that is a number
.\build\x64-release\bin\rawpage_bench_render.exe   # A2's PROBE figure (S2 T3), raw PDFium below the
                                                    #   facade. Add `8 --unsafe-parallel` to reproduce
                                                    #   I-05 -- it is EXPECTED to crash
.\build\x64-release\bin\rawpage_bench_save.exe     # D3-4's and D3-16's cost (S3 T8): save after one rotation and
                                                    #   the whole-file hash, on pages-900.pdf and a generated
                                                    #   200 MB file. Reports; never fails on speed
.\build\x64-release\bin\rawpage_bench_surface.exe   # A2 through the SHIPPED PANES (S4 T14, D4-16):
                                                    #   CompositionRoot and MainWindow as main.cpp builds
                                                    #   them, over pages-900.pdf, with the real
                                                    #   ThumbnailsPane, SplitViewContainer and TilePresenter
                                                    #   issuing every request and the harness issuing none.
                                                    #   NOT a flag on bench_pipeline, on purpose: that one
                                                    #   reports T2's SYNTHETIC pattern and this one the
                                                    #   product's, and a switch that printed either under one
                                                    #   name would make D2-1's error available again. It
                                                    #   PRINTS THE DPI AND VIEWPORT IT REACHED -- it took the
                                                    #   wrong measurement twice before the right one (A-72).
                                                    #   Its fifth column, `+ loupe`, is S4 D4-20's A2
                                                    #   check: the sharp lens inside the graded lane (A-100).
                                                    #   And since S5 T14 (D5-22, S5 guide A-112) its Stage 5
                                                    #   columns: text-900.pdf with the find bar typed into,
                                                    #   the search pane searching a corpus copy, and a
                                                    #   comparison -- each graded on the tiles delivered
                                                    #   WHILE its work ran, beside the viewer alone over the
                                                    #   same rounds. `--stage4` / `--stage5` run one group,
                                                    #   `--repeat N` the Stage 5 runs (3)
.\build\x64-release\bin\rawpage_bench_pipeline.exe # A2's PRODUCT figure (S2 T18), through the shipped
                                                    #   scheduler and cache -- plus O7's and D2-14's
                                                    #   numbers. `8` sweeps the worker count;
                                                    #   `--stage4` is S4 T2's workload and `--stage5` is
                                                    #   A2 AT STAGE 5 (S5 T3, D5-22): the viewport beside
                                                    #   an index build, a folder search and a comparison,
                                                    #   each job stamped on the worker. Write its output to
                                                    #   a FILE: a pipeline cut short by `Select-Object
                                                    #   -First` leaves the bench RUNNING, and two at once
                                                    #   skew each other's figures (S5 T3);
                                                    #   `--whole-page` reports the other quantity.
                                                    #   READ IT WITH THE PROBE, NEVER INSTEAD OF IT
.\build\x64-release\bin\rawpage_bench_openclose.exe --bridge 50   # Stage 2 I-35's reproducer, NO window: LIVE
                                                    #   heap per round of ten documents, and the
                                                    #   block sizes that grew. `--no-render`
.\build\x64-release\bin\rawpage_bench_openclose_ui.exe 30 --native --with-encrypted --bus --quiet-log
                                                    # guide case A-92: the product's own composition,
                                                    #   real platform and style (a window appears,
                                                    #   without taking focus). Read liveHeapMB, NEVER
                                                    #   privateMB alone -- it swings 180 MB on retention.
                                                    #   Offscreen reads differently: see the header.
                                                    #   _classicheap is the same WITHOUT the product's
                                                    #   manifest: the fragmentation control (I-35)
.\scripts\check-single-instance.ps1                 # A-61's two-process half, with its `true` control
```

`check-single-instance.ps1` **edits `%APPDATA%\RawPage\settings.ini` and restores it in a `finally`**,
which is exactly why it is not a CTest case: a suite that rewrote the settings of whoever ran it would
be a worse defect than the one it checks. It has to, because Qt resolves `AppDataLocation` through
`SHGetKnownFolderPath` — measured at T15 — so overriding `APPDATA` for a child process does *not*
redirect the file.

```powershell
.\build\x64-release\bin\layer-lint.exe    --root . --explain   # includes, rules, 15 live waivers (S5 T14)
.\build\x64-release\bin\i18n-lint.exe     --root . --explain   # locales, shipped flags, unfinished count
.\build\x64-release\bin\mutation-lint.exe --root . --explain   # rules, blind spots, each command's row and tags
.\build\x64-release\bin\license-audit.exe --manifest vcpkg.json --spdx-map tools\license-audit\spdx-map.json `
    --qt-modules build\x64-release\qt-modules.txt --compiler-plugins build\x64-release\compiler-plugins.txt
    # license-audit has NO --explain and NO --help: an unknown or missing argument prints its usage and
    #   exits 2 (measured 2026-09-14). --target checks passes it these arguments, plus
    #   --vcpkg-installed when an installed tree exists -- cmake/RawPageChecks.cmake is the invocation
.\build\x64-release\bin\ribbon-validate.exe --explain   # NOT a gate -- a build step (D1-8)
```

**Three notes on running `rawpage.exe` from PowerShell**, each of which cost time to rediscover.
PowerShell does **not wait** on a GUI-subsystem binary, so `$LASTEXITCODE` after `& .\rawpage.exe`
reads the *previous* command's — use `Start-Process -Wait -PassThru` and read `.ExitCode`. And an
ordinary launch now runs an event loop and does not return; `--dump-ui-strings` is the switch that
composes everything and exits. And the three switches that compose the product — `--dump-ui-strings`,
`--print-screen-metrics`, `--list-actions` — **do not claim the single-instance bus**, so they answer
whether or not a window is open (`Intent::composesAndExits()`, S2 issue **I-27**); before 2026-09-13
they forwarded and printed nothing, and a walker with the product open got an empty answer and an
exit code of 0.

Presets: `gates` (tools + gate fixtures only, **no Qt and no vcpkg** — what CI runs first),
`x64-debug`, `x64-release`, `arm64-release`, plus the two non-Windows ones — `portable-asan`,
`portable-fuzz`. **`portable-fuzz` has had targets since Stage 2 T5** — `fuzz_cos_lexer`, and
`fuzz_cos_parser` from T6, which is the primary one — and **`fuzz_cos_roundtrip` from Stage 3 T13**, the reader
and the writer against each other, seeded also from `rawpage_fuzz_seeds`' output (a helper built on
every preset, `tests/fuzz/seeds/`) — and **`fuzz_text_pipeline` from Stage 5 T4** (D5-21), `core/text/`'s
analyzer and normalizer over synthetic hostile geometry. It needs clang and libFuzzer, neither of which is
on this machine, so it runs only on Linux CI, in two places: `ci.yml`'s `portable-fuzz` job runs each
target for 60 s on every push, and **`.github/workflows/fuzz-soak.yml` is the ten-minute soak** (guide
cases A-25 and A-26). The soak runs nightly and by hand, never on push:
`gh workflow run fuzz-soak.yml -f seconds=600`. Its first run, 34781013867, was clean over two targets; over three,
run 35134357858 found Stage 3 guide I-13 and run 35137107236, on the fix, was clean. `tests/fuzz/CMakeLists.txt` fails configure when its target count is
wrong — **1 at T5, 2 from T6, 3 from Stage 3 T13, 4 from Stage 5 T4**, asserted by `cmake/RawPageFuzzTargets.cmake`, whose
`gates.fuzz_count.*` cases prove it fires — because "ten minutes with no crashes" is passed trivially by a
target that builds nothing.

**Six test targets and eight benchmark targets** (re-derived from the `CMakeLists.txt` files
2026-09-23 at S4 T14, which added `rawpage_bench_surface`; one of the eight is
`rawpage_bench_openclose_ui_classicheap`, the manifest CONTROL for `_openclose_ui` rather than a
measurement of its own), and the split is load-bearing rather than
organizational — a suite is a separate target when linking something would destroy a property
another suite proves. `rawpage_tests` (`unit.`) links no display and no engine, which
`rawpage_assert_headless()` **fails configure** to enforce; `rawpage_platform_tests`
(`platform.`), `rawpage_ui_tests` (`ui.`, offscreen), `rawpage_render_tests` (`render.`, links
the engine), `rawpage_golden_tests` (`render.golden.`, links QtGui for the PNG codec) and
`rawpage_composition_tests` (`ui.composition.`, S3 T11 — the only one linking the engine, the widgets
and the OS bundle together, with `app/`'s composition compiled in) each exist for one such reason. A sixth prefix, `gates.`, has **no binary at all** — it drives the
gate executables and `cmake -P`, which is what lets the `gates` preset run with no Qt and no
vcpkg. A benchmark reports a figure and its exit code is about whether the measurement was
*possible*, never whether the machine was fast.

**Why each target is separate, row by row — and what merging it would destroy — is
[`Implementation-Plan.md` §2.1](claude-plan/Implementation-Plan.md). Live counts live in that
file's stage status blocks.**

Single test / suite — Catch2 v3 binaries registered with CTest, selected by name regex:

```
ctest --preset x64-debug   -R tst_settings --output-on-failure
ctest --preset x64-release -R "^platform\."      # the OS layer's suite (51 cases, re-counted 2026-10-01)
ctest --preset x64-release -R "^gates\."         # the gates against deliberately bad input
ctest --preset x64-release -R "unit.settings.compile"   # the negative-COMPILE fixtures (A-41)
ctest --preset x64-release -R reversibility     # apply+revert restores the structural hash (S3)
ctest --preset x64-release -R "instant" -V      # S5 A-63: text-900.pdf's second search, graded on the guide's 3.3 (~40 s)
ctest --preset x64-release -R "search_settings.restart" -V   # S5 A-68: the search/* keys across TWO processes
ctest --preset x64-release -R "every labelled change" -V      # S5 A-95: the comparer graded against corpus/compare/'s records
ctest --preset x64-release -R "unrelated documents finish" -V # S5 A-97: the bound -- seconds, live-heap peak, and the cap's control
ctest --preset x64-release -R roundtrip         # open, rotate, save, reopen over the corpus (S3 T8)
ctest --preset x64-release -R a4_product -V     # A-57: the product saves the signed corpus, OpenSSL judges (S3 T8)
ctest --preset x64-release -R "platform.recovery.kill" -V   # A-68, A-69: TerminateProcess x20 during commands and saves, and where each kill landed (S3 T9)
ctest --preset x64-release -R "ui.composition" -V          # A-74: a killed probe's journal offered and replayed by CompositionRoot + MainWindow (S3 T11)
ctest --preset x64-release -R "roundtrip (fuzz body|I-12)" -V   # A-83: fuzz_cos_roundtrip's body over the corpus, its controls, and I-12's four regressions (S3 T13)
ctest --preset gates -R fuzz_count                         # A-83(a): the fuzz target count assertion refuses 2 of 3, 0, and a helper standing in
$env:QT_QPA_PLATFORM='windows'; .\build\x64-release\bin\rawpage_ui_tests.exe "[I-14]"   # S4 A-85: SKIPS under ctest (one offscreen screen);
                                                           #   this is its real run -- two monitors at different scales
$env:QT_QPA_PLATFORM='windows'; .\build\x64-release\bin\rawpage_ui_tests.exe "[I-23]"   # S4 A-94: SKIPS under ctest (no frame offscreen);
                                                           #   three windows tile and cascade on the primary for a few seconds
$env:QT_QPA_PLATFORM='windows'; .\build\x64-release\bin\rawpage_ui_tests.exe "[D5-10]"  # S5 A-50: SKIPS under ctest (no system
                                                           #   clipboard offscreen); REPLACES the system clipboard's contents
$env:QT_QPA_PLATFORM='windows'; .\build\x64-release\bin\rawpage_ui_tests.exe "[corner-fit]"  # S5 A-115: the ruler corner's
                                                           #   ink half -- offscreen runs only its size-hint half
```

### Traps in running the suites — each one cost a session

These are about **running** things, which is what this file owns; the defects behind them are in the stage
guides' §8 logs. They survive their stage, so they live here rather than in a status block.

- **A widget case that closes a document must flush `QEvent::DeferredDelete` before its fixture goes.** The
  tab's destructor closes its handle on whatever backend is at that address (S3 T10).
- **A case that composes the product must point `Options::recoveryRoot` at a scratch directory**, or the
  suite writes into — and is offered — the recoveries of whoever runs it (S3 T11).
- **A planted-bug control on a `ui/` file rebuilt through one test target leaves the other binaries that
  link `rawpage_ui` stale.** Rebuild everything before a full run (S3 T11).
- **A planted-bug control that restores a file by moving a backup back leaves ninja's build stale** —
  touch the restored file (S3 T5). And `--target checks` rebuilds no test binary, so a control restored
  before a `ctest` run is still in the binaries until a real build (measured 2026-09-18).
- **A planted-bug control that does not COMPILE leaves the previous binary in place, and the case
  passes over it.** `-Werror` turns a plant's unused function or variable into a failed build; a
  script that greps the build's last lines for an error word misses it, runs the old binary and records
  *stayed green*. Read the build's exit code (S5 T12: three of the first batch's plants did this, and the
  batch was re-run). And a plant script that is KILLED skips its own restore — check `git diff` after one.
- **A Catch2 filter that matches NO case exits non-zero, which a plant runner reads as *fired*.** A
  test name holding `[` (read as a tag) or `'`, handed to the binary through Windows' argument quoting,
  matched nothing and printed *"No test cases matched"* — five of S5 T13's first plants "fired" that way.
  Filter by tag or by a `*wildcard*`, and refuse a run whose output has no `test cases:` line.
- **A gate case that repeats `EXPECT_MATCH` or `EXPECT_NOT_MATCH` asserts every one of them only since
  S5 T13** (S5 guide I-09): before, `cmake_parse_arguments` kept the last and two cases had each been
  checking one of two clauses. A plant that fails `clean_tree` and not the case written for it is the sign.
- **A failed Catch2 `CHECK` on two `QByteArray`s crashes the reporter** while it prints them — compare
  SHA-256 hex strings instead (S3 guide I-09).
- **A failing `ctest` run's output survives only until the NEXT run**: it is in
  `build\<preset>\Testing\Temporary\LastTest.log`, which every run overwrites. Copy that file before
  re-running anything, or an intermittent failure becomes a failure nobody can explain —
  `platform.durable replace control` failed once on 2026-09-27 — its second unexplained red, the first
  at Stage 3 T10 on 2026-09-15 — was re-run, and its cause could not be recovered (S4 guide §9 item 4).
  The case now prints the failing Win32 call and its `GetLastError`, so keep that output.
- **A probe output open in a PDF viewer makes the A4 probe's write fail**: close `build\<preset>\a4-probe\`
  files before running the suite (S3 T3).
- **A corpus PDF held open by a viewer makes `make-corpus.py` stop part-way with `PermissionError`** (S3 T1).
- **A fuzz finding in the round trip is reproduced** by adding its bytes to a `roundtrip I-12`-style case in
  `tests/roundtrip/tst_roundtrip.cpp`, which runs on Windows and under `portable-asan` (S3 T13).
- **A running `rawpage.exe` blocks the release LINK and skips a case.** A window left open holds
  `bin\rawpage.exe` (ninja fails with *"cannot open output file … Permission denied"*, and every
  test binary still links) and holds the single-instance bus, which makes
  `ui.diagnostics_ignore_the_bus` **skip** rather than fail — a second named skip beside the battery
  case. Close the window rather than killing it: a `TerminateProcess` leaves a recovery journal and
  the next launch offers a recovery that never happened (S4 · 2026-09-24, during I-10's fix).
- **Run ONLY the tagged real-desktop cases on `QT_QPA_PLATFORM=windows`, and ask the user to keep
  their hands off the desktop while they run.** Those are Stage 4's A-85 `[I-14]`, A-94 `[I-23]`, A-95 `[I-24]`
  and A-96 `[I-25]`, and **Stage 5's A-50 `[D5-10]`, which owns the SYSTEM clipboard** — it replaces what
  the user had copied, and a copy made elsewhere while it runs is what it would read. An offscreen suite such as `[shell]` run there **opens a real *Open PDF* dialog
  and waits** — offscreen the product refuses to prompt, on a real platform it prompts — and these
  cases create real windows, so a pointer passing over them changes what they see. Both happened on
  2026-09-27: a moving mouse dismissed the dialog and failed an assertion, and the next run hung
  until its test process was stopped (S4 guide A-96's run).
- **Every UI case shares ONE `QApplication` when `rawpage_ui_tests.exe` runs whole — and `ctest`,
  one process per case, hides anything a case leaves behind.** Until 2026-09-27 the whole binary died
  (an access violation, `std::bad_alloc`, or no output) in a different case every run: a closed tab's
  pending `deleteLater()` ran in a later case against a destroyed fixture, and two cases leaked the
  application font (S4 guide **I-28**). **Catch2 v3 SHUFFLES by default**, so reproduce a whole-run
  failure with the `Randomness seeded to:` value it printed — `--order rand --rng-seed <n>` — and
  bisect: a subset keeps its relative order under one seed. `tests/ui/main.cpp`'s
  `ProcessStateGuard` (guide A-103) now fails the leaking case itself under `ctest` when a widget
  survives or the application font or font database changes; **what it does not check — the palette,
  the style, translators — can still leak**, so a case that changes one restores it. And **a name
  holding a comma cannot be selected by that name** (convention 2): escape it `\,` or use a wildcard.
- **A case that composes the product must point its SESSION root as well as its recovery root at a
  scratch directory**, or a quit inside the suite writes the session of whoever runs it (S4 guide
  A-79).
- **A DEBUG binary run directly, in a shell that has not dot-sourced `scripts\env.ps1`, prints nothing and
  exits `-1073741515` (0xC0000135, a DLL not found)** -- the Qt debug DLLs are not beside it -- which reads
  like a crash of the suite and is not a test result at all; the release binaries happened to start (S5 T8,
  running `rawpage_ui_tests.exe` whole). Source the environment first.
- **`rawpage.exe` is a GUI-subsystem binary: PowerShell's pipeline captures none of its output.**
  `$out = & .\rawpage.exe` captures nothing; read a diagnostic through `cmd` redirection (S4
  guide A-05, at T14).
- **A `Qt::UniqueConnection` on a LAMBDA slot is a debug-only fail-fast and a release-only
  silence** — `x64-debug` catches what `x64-release` reports green over, which is one reason the
  two presets are not interchangeable (S4 guide **I-09**).
- **A bench that measures the product must print the dpi and the viewport it actually reached** —
  `bench_surface` took the wrong measurement twice before the right one (S4 guide A-72).
- **A program that composes the product without `app::Application::loadTranslators` lays its widgets out
  in catalog KEYS, and offscreen font metrics are about twice the `windows` platform's** (an average
  character 12 px against 6, measured at S5 T14) — so a width measured offscreen is not Windows' width.
  Both put the find bar's minimum at thousands of pixels in `bench_surface` before the real defect under
  them, S5 guide **I-10**, was measured on `QT_QPA_PLATFORM=windows`. Claim a size from that platform.
- **A hidden window's page views still ask for tiles.** A hidden comparison window's panes held 65
  tiles each (S5 T14), so hiding a window is not a way to keep its requests out of a measurement.
- **A warning census from an incremental build is inherited, not measured** — a built tree compiles
  nothing and prints *"ninja: no work to do"*. Use `--clean-first` when the claim is about warnings
  (S3 guide A-01, which measured 232 objects per preset then; **303 release and 292 debug on
  2026-09-27**, a clean build of each — 302 and 291 at Stage 4 T14, 296 and 285 after T13 — and
  **330 C++ objects on each preset at Stage 5 T14** (329 debug at T13, 324 at T12), so re-derive rather than quote).
- **A target that does not call `rawpage_common_flags` is not compiling this project's language, and
  it will build until the day it does not.** Four targets had been compiled at the compiler's default
  standard — `rawpage_fuzz_seeds` since Stage 3 T13, the three `tests/fuzz` drivers since Stage 2 T5,
  those three because they skip `rawpage_strict()` to avoid `-Werror` and lost the standard with it.
  **`rawpage_assert_language_standard(20)` now refuses to configure such a target**, so this is a gate
  rather than a thing to remember (S4 guide **I-08**). Two bounds: it checks the standard alone, and it
  walks only the graph the current preset configures — the three fuzz drivers exist only under
  `RAWPAGE_BUILD_FUZZERS`, so a Windows configure walks **33** targets (re-counted 2026-09-27 from
  the configure's own `targets walked` line; 32 at T12, before T14's `rawpage_bench_surface`) and that
  flag makes it 36. The `gates` preset walks 5.

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

## Commit messages

**Adopted 2026-10-01**, from [joelparkerhenderson/git-commit-message](https://github.com/joelparkerhenderson/git-commit-message/).
Until then every commit here put its whole message on the summary line, so `git log --oneline`
printed a paragraph per commit (`33152c4` is the last of them). Earlier commits are not rewritten.

- **The summary line: at most 50 characters, capitalized, starting with an imperative present-tense
  verb, and with no closing period.** Write `Add the text index and search`, not
  `added text index.` and not `Stage 5 T6, 2026-10-01: SearchQuery, SearchEngine and …`.
- **The verb says what kind of change it is.** Use one of these:
  - **Add** / **Drop**: create or delete a capability (a feature, a test, a dependency).
  - **Fix**: fix an issue (a bug, a typo, a misstatement).
  - **Bump**: raise a version.
  - **Make**: change the build, the tools or the infrastructure.
  - **Start** / **Stop**: begin or end doing something (a toggle, a flag).
  - The **pure** verbs are a promise that the commit does nothing else:
    - **Optimize**: performance only.
    - **Document**: documentation only.
    - **Refactor**, **Reformat**, **Rearrange**, **Redraw**, **Reword**, **Revise**: only that kind of change.
    - **Refresh**: test data, keys and the like.

  A commit that touches behaviour is not `Document` however much of it is prose.
- **No tags in the summary.** No `docs:`, no `[bug]`, no issue or task label. The stage task, its
  date and its home go on the **first line of the body**, for example
  `Stage 5 T6 (Stage5-TextSearch-Plan.md §6), 2026-10-01.`, so `git log --grep "Stage 5 T6"` still finds it.
- **A blank line, then the body, wrapped at 72 columns.** Atypical text is left unbroken and may run
  longer: a test name, a path, a command, measured output, a URL.
- **The body says what changed and why**, and carries what this project's commits always have:
  - what was measured, with its figures;
  - the test counts, as a figure from a named run (rule 3 above);
  - the planted controls, and any that stayed green;
  - the deviations;
  - which documents were updated.

  It summarizes and points. The facts stay with the documents that own them (the routing table),
  and every label cites its home (*Citing a label*).
- **No trailers unless the user asks for them**: no `Co-authored-by`, no attribution lines. A reference
  to anything outside the repository is a fully qualified URL in the body (`See: https://…`).
- **Check the summary's length before committing:** `git log -1 --format=%s` after
  the commit, or count it before. Amend only a commit that has not been pushed.

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

**A2 is SETTLED** (Stage 2 T3 and T18): **`go`** on both of D2-1's measurements, and re-measured on
2026-09-14 under the segment heap, which moved it further in its favour. Still open: **A1** (Write mode
is a first-party reflow engine) and **A4** (does a signature survive a later unrelated incremental
save, Stage 3) — whose probe verdict is `go` and whose product verdict is recorded as partial, both over
RawPage's generated signatures only (`Tech-Stack-Plan.md` §3.3). **A5**, **A6** and **A7** were settled in Stage 0 — we build with Qt's own bundled GCC
13.1.0 and `toolchain-mingw.cmake` keeps it that way. Risks **R5** and **R6** are retired with A2 and
A3. Larger unknowns carried forward: the Write-mode reflow engine (R1), a first-party full-text index
since Xapian is GPL (R2), Office export fidelity (R3), dynamic XFA (R4), the sanitizer/PDB capability loss from the
GCC toolchain (R7), and the MSVC-shaped corners of the Windows integration surface — print driver,
Office add-ins, arm64 (R8). Open decisions are **O1, O3, O4, O5, O6, O8 and O11** (O2, O7, O9 and O10
are resolved), listed with their owning stages in `Implementation-Plan.md` §8 — if work forces one,
record the resolution there rather than deciding it inline. *(States re-read from the owning tables on
2026-09-14; read them there.)*
