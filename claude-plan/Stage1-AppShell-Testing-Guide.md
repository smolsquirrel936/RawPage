# Stage 1 — App Shell & Action Layer · Testing Guide

> **Authoritative verification document for Stage 1.** Written from
> [`Stage1-AppShell-Plan.md`](./Stage1-AppShell-Plan.md) and
> [`Implementation-Plan.md` § Stage 1](./Implementation-Plan.md#stage-1--app-shell-action-layer-settings-and-single-instance),
> **before any Stage 1 code exists** — which is the point: verification written afterwards only ever
> confirms whatever got built, including the parts built wrong.
>
> **Status:** 🟡 **Automated half GREEN IN CI at 71 of 73 — Stage 1 T1…T15 verified as of 2026-08-25.
> Two cases cannot run and are carried to Stage 2 (see the row below); the manual walk remains.**
> **73 automated** cases
> (A-01 … A-94, plus **A-67** added at T7 and **A-90 … A-94** added at T14 in a new §5.10) and **12
> manual** (M-01 … M-12), of which **M-08 is withdrawn** with the accessibility removal.
>
> | | |
> |---|---|
> | **Automated, green** | **71 of 73**, every one ticked against evidence re-verified on 2026-08-25 — see §5's reconciliation note for which evidence covers which case. `ctest` **334/334** on both presets (328 at T14); the bare `gates` preset **66/66** with no Qt; `checks` clean on **three** gates at 141 files / 930 includes, i18n-lint 78 files / 2 catalogs / 0 waivers. **A-85's figure: p95 = 0.471 ms against R-9's 16.667 ms budget**, with a control proving the instrument was live. **A-80 closed** on run 32785493072 — four jobs green, and **the `windows` job ran for the first time in the project's history** at 334/334 (`gates` 52/52, `portable-asan` 196/196) |
> | **Automated, cannot run — 2 of 73** | **A-25** names a `rawpage.exe --list-actions` switch that **does not exist** and never has; **A-26** needs an alias fixture that was never written, for a mechanism (`RAWPAGE_ACTION_ALIAS`) that is real code with **zero rows and no test**. Neither is a product defect and neither is closed: both are marked in place with what *is* covered by other means, and **both are carried to Stage 2**. Until T15 this row did not exist and both cases were counted as green (**I-32**) |
> | **Manual** | **M-01 ✅** and **M-02 ✅** walked and signed off by the user — M-01 on 2026-08-23, M-02 on 2026-08-23 for the ribbon and **re-walked in full on 2026-08-30** (all four steps, no findings). **M-03 ✅ walked and signed off 2026-08-30**, and it is the case that proved the manual half earns its place: it found **I-33, I-34 and I-35** — the stage's first three defects in the *product* — where `tst_documenttabs.cpp` had asserted everything those gestures end in and stayed green throughout. **M-12's keyboard walk became walkable at T13** — every §8 Global chord now reaches its command, and what the harness structurally cannot see is a chord the window manager or the platform eats before Qt sees it, which is exactly what that walk is for. **M-11 became fully walkable at T14**, and so did **M-02's non-ribbon half** — its steps name the sidebar, a floating pane **and Preferences**, and T14 made all three real, so that case could be re-walked as written rather than partially. **That re-walk happened on 2026-08-30 and M-02 is now ✅.** **M-04 ✅ walked and signed off 2026-08-30**, after three passes and four findings — each repair making the next defect reachable: **I-36** (S2) stopped it at its own first step — there was no View tab to click, because the ribbon's whole tab strip had been laid out zero pixels wide since T10 — then **I-37** (S3), an auto-hidden pane that only a click could reveal, and **I-38**, a `touchMode` switch that reads back and does nothing, which the user reclassified as a planned absence rather than a defect. The case was re-walked from step 1 after I-36, per M-03's rule. **M-05 ✅ walked and signed off 2026-08-31** — mis-listed as unwalkable until 2026-08-30, then it produced **I-40** (S3, unreadable disabled text in both themes, light the worse of the two) and **I-41** (S2, the native style painting control text from its own colour scheme and ignoring the palette), the second found only because the first fix changed nothing on screen. **M-06 ✅ signed off 2026-08-31** after four defects — **I-42**, **I-44**, **I-45** and, surfacing here but owned by M-11, **I-43**. M-07, M-09 and M-10 are still not walkable: the code they test does not exist |
>
> **Issue log: I-01 … I-35. None open.** The last three were the user's M-03 walk on 2026-08-30 and
> all three were **in the product** rather than in the harness — Stage 1's first of those:
> **I-34 (S2)**, re-dock could not be performed at all because the gesture reported at the 40 px
> crossing instead of at the release; **I-33 (S3)**, tearing a window's only tab out left it empty;
> and **I-35 (S3)**, a window emptied by a re-dock stayed open — which **only became reachable once
> I-34 was fixed**, and is why a repaired case gets re-walked rather than signed off on the strength
> of the repair. All closed the same day: five new cases, five negative controls.
> Everything before them is closed —
> I-04 closed at T8, I-12 … I-16 at T8/T9, **I-17** by the
> first `portable-asan` run to reach Stage 1's code, **I-18 … I-20 by T10's first run against a
> real, shown window**, **I-21 … I-23 at T11 and T12 — every one of the three by running
> something new against the product rather than by reading it** — and **I-24 and I-25 at T13, both
> of them the harness rather than the product**: a case that asserted the opposite of what it meant,
> and a gate that flagged a diagnostic string — and **I-26 … I-28 at T14**, every one of them found the
> same way: a remap that *added* a chord instead of replacing one, a gate about to report forty notes
> nobody could act on, and a log file that could not name the binary that wrote it — and **I-29 and
> I-30 at T15**, both of them again the harness and its infrastructure rather than the product: a
> benchmark whose every figure was its own argument-evaluation bug, and a CI job that had never run
> because two systems had to agree on a variable name and nothing checked that they did. **I-31**
> closed the same day and is the exception that names the rule: it was found by *reading* — §7's
> "do not report this as a bug" table had two rows that were wrong, and a wrong row there does not
> merely misinform, it tells the walker that a real finding is expected behaviour. **I-32 is the same
> failure one level up** — the guide's own summary claimed 73 cases green while 46 of them carried no
> recorded result and two could never have run — and it was found the same way, by a question rather
> than by a run. **Twenty-five of the thirty-two were found by running something rather than by
> reading it**,
> which is by now less a pattern than a method: **I-05** and **I-06** by the user's manual walk,
> **I-03** by reading output rather than an exit code, **I-08** and **I-09** by settings fixtures
> written the way a person would hand-edit the file, **I-11** — an S2 race in the single-instance
> handoff — by the platform suite on its first run, **I-12** — an S1 segfault on window destruction
> — by the widget suite on *its* first run, **I-13**, **I-14** and **I-16** by running the composed
> application and then looking at what it had actually written, and **I-17** — a dedup rule that was
> right on Windows and wrong on Linux — by the CI job that had not run since before T3. See the
> [issue log](#8-issue-log).
>
> Two halves, per [`CLAUDE.md`](../CLAUDE.md): **[§5 Automated](#5-automated-verification--i-run-this)**
> — I run it and report the actual output — and **[§6 Manual](#6-manual-verification--you-run-this)** —
> you walk it on real hardware, and **I never mark a manual case passed on your behalf.**
>
> This stage's manual half is **much larger than Stage 0's, on purpose.** Stage 0 was a build system,
> and almost all of it was automatable. Stage 1 is a window. Ribbon sizing, a mid-session DPI change,
> tear-off feel, key-tip navigation, and whether a Traditional Chinese label fits its button are not
> things an offscreen run has any access to — and the A3 go/no-go rides on exactly those.

---

## 1. Scope

### In scope — must pass for sign-off

The four things Stage 1 exists to guarantee
([plan §1](./Stage1-AppShell-Plan.md#1-what-stage-1-is-actually-for)): every input path resolves
through `ActionRegistry`; one typed settings store exists and an undeclared key does not compile; the
UI is translatable and `zh-TW` is real; and **A3 has a recorded verdict**.

### Out of scope — do NOT file these as bugs

Planned absences ([plan §11](./Stage1-AppShell-Plan.md#11-explicitly-out-of-scope)). Filing them
wastes a triage cycle and invites someone to "fix" them early.

| Not present at Stage 1 | Arrives at |
|---|---|
| Any PDF is opened, parsed, or drawn. `Ctrl+O` opens a **stub tab** showing a filename and a placeholder canvas | Stage 2 |
| `Ctrl+Z` / `Ctrl+Y` do anything visible. **They do not "dispatch to an empty journal"** — corrected at T15, and this was the third copy of that wrong claim (with §7 and `Stage1-AppShell-Plan.md` §11). Both chords are *bound* and resolve, but `EditUndo`/`EditRedo` are not among the nineteen registered ids, so `dispatch` answers `NotFound` and the key is left **unconsumed** — which is why a `QLineEdit` keeps its own undo | Stage 3 |
| `mutation-lint` — **three** of the four gates exist after this stage | Stage 3 |
| Contextual ribbon tabs with real content; the mechanism only is proven | Stages 5–13 |
| Session restore of *documents*; window geometry only | Stage 4 |
| Preferences beyond **General, Logging and Shortcuts** — 26 groups exist, **three pages ship**. *(This row said "beyond General and Shortcuts" until T15 and so named only two of the three; Logging ships because T14 also ships the log pane, and `logging/level` and `logging/subsystems` decide what that pane can ever show.)* | Stage 14 |
| **`general/touchMode` changing any hit target.** The key ships — `SettingsKeys.def` declares it and the General page offers it — and no code reads it. Added 2026-08-30 from M-04's walk (**I-38**), so the switch is a stored preference with no consumer until the stage that owns touch input | Stage 14 |
| Thumbnail provider, preview handler, jump list, index filter | Stage 15 |
| `zh-CN` and `ja` catalogs — declared `"shipped": false` on purpose (D1-14) | Stage 15 |
| Golden-image, round-trip, and fuzz findings — those suites have no subject yet | Stages 2–3 |
| ~~A full screen-reader pass~~ | ⤵ **Withdrawn 2026-08-23** — accessibility is out of the product entirely, not deferred to Stage 14. See [`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23) |

### Case anatomy

Every case is **ID · Goal · Pre · Steps · Expect · If it fails**, with a `☐` you change to ✅ or ❌.
A case either passes or yields a **specific** failure — "looks fine" is not a result.

### Severity

| Sev | Meaning |
|---|---|
| **S1** | An input path bypasses `ActionRegistry`; a settings key escapes the schema; a gate does not fail on a violation; **A3 cannot be judged**. The shape the next fifteen stages are poured into is wrong |
| **S2** | A documented behaviour is missing or wrong in a way a later stage will build on — a chord resolves to the wrong context, the ribbon cannot be built from JSON, single-instance forwards but does not raise |
| **S3** | A diagnostic is unclear, a label is truncated, a documented command is wrong, visual polish that a user would notice |
| **S4** | Polish nobody outside the team would notice |

**An S1 here is different from an S1 at Stage 0.** There it meant the safety net was broken. Here it
means the *architecture's load-bearing claim* — one dispatch path, one settings store — is already
false, and every later stage will be written against a lie.

---

## 2. Build, launch, environment

`cmake`, `ninja`, and `g++` are **not on this machine's `PATH`**. Every command below assumes
`scripts\env.ps1` has been dot-sourced first, and a case that works only *without* it is itself a
finding.

```powershell
. .\scripts\env.ps1
cmake --preset x64-release
cmake --build --preset x64-release --target checks      # THREE gates now
cmake --build --preset x64-release
ctest --preset x64-release --output-on-failure
.\build\x64-release\bin\rawpage.exe                        # a window opens — this is new
```

**Two things about that last line, both of which cost time to rediscover.** The executable is in
**`bin\`** — this section said `.\build\x64-release\rawpage.exe` until T15, which is a path that has
never existed and would have failed for whoever walked it first. And PowerShell does **not wait** on a
GUI-subsystem binary, so `$LASTEXITCODE` after `& .\rawpage.exe` reads the *previous* command's; use
`Start-Process -Wait -PassThru` and read `.ExitCode`. An ordinary launch runs an event loop and does
not return at all — `--dump-ui-strings` is the switch that composes every service and exits.

**What "launched" looks like at this stage:** a window with a ribbon, an empty document tab bar, a
sidebar dock, and a status bar. `Ctrl+O` opens a **stub tab** — a filename and a grey placeholder
canvas, with no page on it. That is D1-2 working correctly, not a rendering failure.

### Settings keys that affect these cases

The keys Stage 1 introduces
([plan of record](./Implementation-Plan.md#stage-1--app-shell-action-layer-settings-and-single-instance)).
This table replaces Stage 0's environment-variable table, and every one of these is a thing a case may
have to change and **must change back**.

**Twelve when this was written; fifteen declared now** — counted from `core/settings/SettingsKeys.def`
at T15 rather than from this table, which had drifted. T11 added the two `ui/` keys at the foot of the
table; they are user choices made from the Quick Access Toolbar's own context menu rather than from
Preferences, which is why they are `Internal` in the schema and get no Preferences row (T14).

**Read the row count carefully, because it does not equal the key count and never did.** The table
below has fifteen rows, but one of them — `shortcuts/<context>/<action>` — is **not a declared key at
all**. It is D1-10's key *family*, generated over `KeyContext` × `ActionId`, and it exists precisely so
that there is no string-keyed API to declare it with. The remaining fourteen rows plus
`meta/schemaVersion`, noted below the table, are the fifteen.

| Key | Default | Effect | Cases |
|---|---|---|---|
| `general/theme` | `FollowSystem` | Light / dark / follow-system. *(OS high-contrast deference withdrawn 2026-08-23 with D1-15.)* | A-52, M-05 |
| `general/uiFontScale` | `1.0` | UI font scale, 0.75–2.0, clamped on out-of-range | A-43, M-04 |
| `general/touchMode` | `false` | Larger hit targets — **stored, and read by nothing until Stage 14** (§1, I-38) | M-04 |
| `general/restoreSession` | `true` | Reopen window layout on launch | A-44, M-11 |
| `general/singleInstance` | `true` | Whether a second launch forwards or opens its own window | A-60, A-61, M-07 |
| `language/uiLocale` | system | Which catalog loads; `--lang` overrides for one run | A-72, M-06 |
| `shortcuts/<context>/<action>` | §8 defaults | Per-context remaps (key **family**, D1-10) | A-35, A-36, M-09 |
| `logging/level` | `Info` | Log verbosity; the rule that document text never appears above debug | A-45 |
| `logging/subsystems` | all | Per-subsystem filtering | A-45 |
| `logging/retainFiles` | 5 | Log rotation depth | A-45 |
| `documents/recentCount` | 10 | Recent-list length | A-46 |
| `documents/recentPinned` | empty | Pinned entries survive eviction | A-46 |
| `documents/recentList` | empty | The recent-files list itself. **Added to this table at T15** — it had been a declared key with no row since T5, which is the same class of gap as the counts above (**I-32**) | A-46 |
| `ui/qatCommands` | *unset* | The Quick Access Toolbar's commands, in order, as action **string ids**. **Unset and empty are different**: unset means "never customized, use the factory three", empty means the user removed them all (`Settings::isSet`) | A-55, M-11 |
| `ui/qatBelowRibbon` | `false` | Whether the toolbar sits below the ribbon instead of above it | A-55, M-11 |

`meta/schemaVersion` (D1-16) is written to the same file but is **not** a user setting and has no
Preferences row — it is internal state, exercised by A-47.

**Every case that writes settings must restore them.** `%APPDATA%\RawPage\settings.ini` persists across
runs, and a case that leaves `general/theme` on dark makes M-05 unreproducible for whoever walks it
next. Cases that need a clean store say so in their Pre and use a scratch `%APPDATA%`.

### Test rig

Much larger than Stage 0's, because this is the first stage with a user interface.

- **The primary machine** for everything automated.
- **Two monitors at different scale factors** — e.g. 100 % and 150 % — with at least one able to have
  its scale changed *while the app is running* (**P-1**). This is the rig for **M-02**, and the
  mid-session change is the half that finds real bugs; a static two-monitor setup is not enough.
- ⤵ ~~A screen reader (**P-2**)~~ — **withdrawn 2026-08-23** with R-8 and M-08; accessibility is out
  of the product. See [`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23).
- **A reference ribbon** — Right PDF Professional or Office on the same machine (**P-4**). R-1 and
  R-4 are judged against it.
- **Your reading of Traditional Chinese** (**P-3**) for M-06. A lint can count keys; only a person can
  see that a label is wrong or does not fit.
- **Two `.pdf` files with awkward names**: one containing CJK characters and one containing an emoji.
  These are the D1-3 evidence (M-07) and they cost nothing to prepare.
- No hex editor, no VM, no token, no scanner. Those rigs belong to Stages 3, 10, 11 and 16.

---

## 3. Verdicts this guide must produce

Sign-off is not just green cases. Four answers must exist in writing, in their owning documents
([plan §8](./Stage1-AppShell-Plan.md#8-verdict-ledger--what-stage-1-owes)):

| Verdict | Produced by | Recorded in |
|---|---|---|
| **A3** — can Qt Widgets host a real ribbon? | M-01 (R-1…R-5, R-7), M-02 (R-6), A-85 (R-9). *(M-08/R-8 withdrawn 2026-08-23.)* | ✅ **Recorded 2026-08-23** in `Tech-Stack-Plan.md` §3.3, beside A5 and A6: **`go, with named custom work`** |
| **`-municode`** — needed, or already solved by Qt (D1-3) | A-05, M-07 | `Implementation-Plan.md` Stage 1 status, resolving Stage 0's deviation (a) |
| **§8 collision census** — the true class-1 and class-2 counts | A-31, A-32 | **I-01**, resolved into `Features.md` §8 by you |

**A3's verdict is graded on the three-point scale in
[plan §5.3](./Stage1-AppShell-Plan.md#53-the-a3-spike--criteria-fixed-before-it-starts)** — *go*,
*go with named custom work*, or *no-go*. "Go with named custom work" is a **pass**: a custom `QLayout`
inside Qt Widgets is ordinary Qt, not a fallback. Only a criterion the widget system cannot meet at
all is a no-go, and the fallback is still Qt Widgets.

A stage that is green but has no verdicts is **not** signed off.

---

## 4. Traceability — exit criterion → case

| [Exit criterion](./Implementation-Plan.md#stage-1--app-shell-action-layer-settings-and-single-instance) | Cases |
|---|---|
| The window opens with a functional ribbon | A-50, A-51, M-01. ✅ **A-50 and A-51 green at T10** — 9 tabs, 49 groups, **192 controls**, all of it from `res/ribbon.json`, and asserted in both directions: everything declared is present *and* no control exists that was not declared, which is the half that actually protects D1-8. A-51 was verified by planting `"file.saevAs"` and watching the build stop with `res/ribbon.json:44: unknown action id`. **M-01 is walked and signed off**, so this criterion is complete |
| **Every** button, menu item, and chord dispatches through `ActionRegistry` | A-12, A-20 … A-24, A-53, A-54, M-09. ✅ **True of everything that exists as of T14, chords included** — the ribbon's 192 controls, the Quick Access Toolbar's buttons and its whole context menu, the tool finder's results, every tab and window operation, every §8 chord, which resolves through `KeyBindingStore` and calls the same `dispatch(ActionId, KeyContext)`, and T14's two — `file.preferences` and `pane.log`, the latter the second toggle in the product. **`file.preferences` needed no enablement guard**, which is the difference between a modeless dialog and `file.open`'s modal picker (issue I-23) and is why it dispatches under `offscreen` in a test. **Enforced twice, statically and dynamically, in both of its forms:** no bare `QAction` (A-12's lint rule, A-54's self-check) and no action that resolves its own key — a `QAction` shortcut or a free-standing `QShortcut` would work perfectly while bypassing the context stack and the user's remap, so `showEvent` reports both and A-53 plants one of each. *(Historic note, kept because it dates the claim:)* **True of everything that existed at T9** — the sidebar's close button dispatches `view.toggleSidebar` rather than calling `hide()`, and `ui.toggling the sidebar goes through the registry` asserts it. The *enforcement* (A-12's lint rule, A-54's self-check) arrives with `ActionFactory` at T10, and chords at T13 |
| A debug assertion catches a directly-connected `QAction` | A-12, A-54. ✅ **Green at T10**, and both halves of D1-7: `layer-lint`'s non-escapable `bare-qaction` rule across four spellings with two controls, and the show-time self-check run against **planted violations** — because a self-check nobody has seen fail is a self-check nobody knows works. **T13 added a second one**: an action carrying its own key sequence, or a free-standing `QShortcut`, is a chord that bypassed the context stack, and one of each is planted in `tst_context.cpp` |
| The §8 Global defaults all resolve correctly | A-30, A-33, A-34. ✅ **All green 2026-08-23 at T4**, with D1-9's document parse negative-controlled in both directions |
| Settings round-trip | A-40 … A-47, M-11. ✅ **A-40 … A-47 green 2026-08-23 at T5**, and the round trip closed at **T8**: nothing had ever called `Settings::sync()` (**I-14**), so every preference was correct for the life of the process and gone at the end of it — found by running flow B and then reading the file. **M-11's QAT half became walkable at T11** — customize the toolbar, move it below the ribbon, quit and relaunch — **its chord-remap step at T13**, and **its Preferences step at T14**, so that case is now walkable end to end. **A-90 … A-92 join this row**: the Preferences surface is generated from the schema, a change made through a control reaches the file, and a remap reaches both the store and the file. A-41 is asserted three ways plus a positive control |
| `--lang zh-TW` shows a translated UI | A-70, A-71, A-72, M-06. ✅ **Both halves complete — M-06 walked and signed off 2026-08-31.** A-70/A-71 green at T6 and re-run at every stage since; **as of 2026-08-31 both catalogs stand at 536 keys each** — 360 `action.*`, 70 `ribbon.*`, 62 `prefs.*`, 34 `shell.*`, 10 `error.*` — with `i18n-lint` clean over 78 files, 0 waivers. A-72 dumps **526 keys per locale, 512 differing between `en` and `zh-TW`, 14 identical and 0 rendering as a raw key**. **The 14 are each explainable and were checked rather than waved through**: the nine `ribbon.keytip.*` access keys, which are deliberately the same Latin letters in both locales (I-42), the overflow chevron glyph, the product name, and two placeholders. **The dump grew by 9 on 2026-08-31 for a reason worth keeping**: `ribbon.keytip.*` is derived from the tab id, so `i18n-lint` sees only the namespace reference `"ribbon.keytip."` in the source (I-27) and cannot check the nine members one at a time — `Spec::catalogKeys()` enumerates them so A-72 does. A family nothing enumerates can lose a member in one locale and stay green everywhere. *(The figures this row carried before T15 — 389 keys, then 450, then "380 strings dumped" — were each true when written and were never revised; corrected under **I-32**.)* **M-06** is the editorial review of the **29** entries marked unfinished — the row said 28 until 2026-08-31 and was never re-counted — which `lrelease` was measured at T9 to *keep* rather than drop, so they do reach a reviewer's screen. **15 of the 29 are on the ribbon** (Protect holds 7 of them, Create 3, Organize 2, Edit, Table Tools and one more), and the other 14 are reachable **only through the tool finder**, which is where that half of the review has to happen. No automated case can judge whether the Chinese is any good |
| A second launch reuses the first instance | A-60, A-61, A-67, M-07. 🟡 **Every automated case is green; only M-07 remains.** A-67 at T7 (the bus's own claim/forward/deliver round trip), A-60 at T8 — a second launch forwarded `測試-📄.pdf` and exited 0, and the primary logged the path byte for byte across three process boundaries — and **A-61 at T15**, in two halves each with its own control: `claim(false)` never touches the bus, and two real processes both stayed alive while the `true` control saw the second forward and exit 0. **M-07** (foreground activation) has no offscreen equivalent and is yours; Windows refuses foreground to a process that received no input, which is the whole reason D1-11 exists |
| **Risk A3 has a recorded verdict** | M-01, M-02, A-85. ✅ **Recorded 2026-08-23: `go, with named custom work`** *(M-08 withdrawn with R-8)* |
| *(plan)* An undeclared settings key does not compile | A-41. ✅ **Green 2026-08-23 at T5** — a misspelled key, a key named by string, and a right key with a wrong-typed value each rejected, with `declared-key.cpp` compiling as the control |
| *(plan)* The ribbon builds entirely from a declarative resource | A-50, A-51. ✅ **Green at T10** — `RibbonTabBuilder` makes no decision the JSON could have expressed, and an unknown id fails the **build**, not the render |
| *(plan)* `platform.h` includes no Win32 header | ✅ **Green 2026-08-23 at T7**, and it is a `layer-lint` layer entry rather than a convention — `gates.layer_lint.platform_header_includes_windows` proves the rule fires, and `…platform_impl_may_include_windows` proves `platform/win/` may still include the OS |
| *(plan)* `checks` runs three gates | A-10. ✅ **Green 2026-08-23 at T6** — and A-11 confirms they still run on the bare `gates` preset with no Qt |

---

## 5. Automated verification — I run this

> **Reconciled at T15, 2026-08-25, and the reconciliation is itself a finding (I-32).** Until then
> this section and its own status header disagreed: the header listed 73 cases as green while **46
> individual cases still carried an unticked box and no recorded result**. The results existed — in
> the per-task rows of `Implementation-Plan.md` — but never made it back into the cases they belonged
> to, because a task's status block is what gets updated when a task finishes and this file is not.
> A guide whose summary is current and whose cases are stale is worse than one that is uniformly
> stale, because the summary is what people read.
>
> Every case below was therefore **re-run or re-verified against the built product on 2026-08-25**,
> and ticked only on evidence:
>
> | Evidence | Covers |
> |---|---|
> | `ctest --preset x64-release` — **347/347**, and the same on `x64-debug` | A-20 … A-24, A-30 … A-37, A-40 … A-47, A-63, A-67, and the `bench.` case |
> | `ctest --preset gates` on the bare preset — **66/66**, no Qt | A-11 … A-17 |
> | `cmake --build --target checks` — layer-lint **141 files / 933 includes**, license-audit clean, i18n-lint **78 files / 2 catalogs / 0 waivers** | A-08, A-10, A-18 |
> | CI run 32785493072, four jobs green | A-09 (portable-asan 196/196), A-11, A-80 |
> | The executable, run and read | A-03 (exit 0, 517 lines), A-04, A-05, A-06, A-07, A-65, A-66, A-70 … A-72 |
>
> **Two cases are NOT ticked and cannot be**: **A-25** names a `--list-actions` switch that does not
> exist, and **A-26** needs an alias fixture that was never written for a mechanism nothing has ever
> exercised. Both are marked in place with what is and is not covered, and both are carried to Stage 2
> rather than closed. They are the reason this section says **71 of 73**, not 73.

Every case names its exact command and its pass condition. I report the **actual** output; a failing
case is reported as failing, with that output, and is never declared fixed without a re-run.

### 5.1 Build, entry point, environment

**A-01 · Configure and build, both presets**
☑ **Goal** Stage 1's sources compile under the pin with `-Werror` in `core/`.
**Steps** `cmake --preset x64-debug` then `--preset x64-release`, build both.
**Expect** Exit 0 on all four. No warning in `core/`, `pdf/`, or `platform/`.
**If it fails** S1 — nothing else runs.

**A-02 · The `ui/` warning grace list is empty**
☑ **Goal** D1-12. **Steps** Read `RAWPAGE_UI_WARNING_GRACE` in `cmake/RawPageWarnings.cmake` after the build.
**Expect** Empty. Every entry present is a finding **with a named reason and an issue number**, or it
does not belong there. **If it fails** S3 — but note that a grace list that starts populated has no
baseline to shrink from, so an unexplained entry here is worth an argument, not a shrug.

**A-03 · The executable starts and exits cleanly under `offscreen`**
☑ **Goal** The property every later stage's headless smoke test depends on.
**Steps** `$env:QT_QPA_PLATFORM="offscreen"; .\build\x64-release\rawpage.exe --version`
**Expect** Exit 0, a version string on stdout, no window, no hang. **If it fails** S2.

**A-04 · Imports are still msvcrt, never ucrtbase**
☑ **Steps** `objdump -p .\build\x64-release\rawpage.exe | Select-String "DLL Name"`
**Expect** `msvcrt.dll` present; `ucrtbase.dll` **absent**; Qt6 Widgets/Gui/Core and the GCC runtime
DLLs present. **If it fails** S1 — the Stage 0 pin has regressed at the moment `ui/` was added.

**A-05 · Non-ASCII argv survives to `QCoreApplication::arguments()` — the D1-3 measurement**
☑ **Goal** Settle Stage 0's deferred `-municode` question by measuring rather than assuming.
**Steps** Launch `rawpage.exe --print-args` with a path containing CJK characters *and* one containing
an emoji, from PowerShell and from `cmd.exe`.
**Expect** The arguments come back **byte-for-byte correct in both shells**, with **no** `-municode`
in the flag set. **If it fails** this is not a bug — it is the *other* verdict: record that
`-municode` (or `Qt6::EntryPointPrivate`) is genuinely required, adopt it, and re-run. **The finding
must be written down either way**; an unrecorded pass here silently re-opens a Stage 0 deferral.

**A-06 · DPI awareness is set before any Qt object exists**
☑ **Goal** Per-monitor-v2 must be established before the first `QGuiApplication`, or Qt caches the
wrong value and M-02 fails for a reason nobody can see.
**Steps** Read `app/main.cpp`; confirm the awareness call precedes every Qt construction. Then
`rawpage.exe --print-dpi-awareness`.
**Expect** Reports `PerMonitorAwareV2`. **If it fails** S2 — and M-02 will fail confusingly.

**A-07 · A build id is embedded, and all three copies of it agree**
☑ **Goal** D1-13. **Steps** `objdump -s -j .buildid .uildd-release
awpage.exe`, then
`.uildd-release
awpage.exe --version`, then the `build id (dump)` line of a `symbolize.ps1` run.
**Expect** A `.buildid` section holding a CodeView **`RSDS`** record — a 16-byte GUID plus a 4-byte age
— and the same 33-hex-character id reported by `--version` and read out of a dump's module list.
**Not** `.note.gnu.build-id`: that is the ELF spelling and this is PE (issue **I-02**), and looking
for it fails a build that is perfectly correct.
**If it fails** S2 — a minidump without a build id cannot be symbolized, which makes the crash handler
decorative. If the three copies disagree, that is **S1**: `symbolize.ps1` would then refuse every
genuine dump, or worse, accept a wrong binary.

**A-08 · `layer-lint` is clean on the real tree at the new size**
☑ **Steps** `cmake --build --preset x64-release --target checks`
**Expect** Clean. Report the file and include counts; they will be several times Stage 0's 27 / 93.
**If it fails** S1 if a real violation exists.

**A-09 · `core/` still builds with no display and no engine**
☑ **Goal** The claim `portable-asan` exists to prove, now that `core/cmd/` and `core/settings/` are real.
**Steps** `cmake --preset portable-asan` and build.
**Expect** Green. **If it fails** S1 — something in the new `core/` code reached for QtWidgets or the
platform layer.

### 5.2 The gates — three of them now

**A-10 · `checks` runs three gates and says three**
☑ **Goal** The comment in `RawPageChecks.cmake` currently says two exist. After T6, three do.
**Steps** `cmake --build --preset x64-release --target checks`
**Expect** Exit 0; `layer-lint`, `license-audit`, **and `i18n-lint`** each report; the target still
**compiles no layer library**. **If it fails** S1 on a real violation; **S2 if `checks` dragged the
whole build in** — that defeats "the gates run first", which is the property the whole CI ordering
rests on.

**A-11 · `checks` still runs on the bare Linux runner**
☑ **Goal** D0-1 extended to the third gate — `i18n-lint` must link nothing but the standard library.
**Steps** `cmake --preset gates` then `--target checks` on a machine with no Qt.
**Expect** Green. **If it fails** S2 — a gate needing the build environment is a gate that runs last.

**A-12 · A bare `QAction` outside `ActionFactory` fails the gate — D1-7(a)** — ✅ **2026-08-24 at T10**
☑ **Goal** The static half of "nothing is wired directly to a widget slot".
**Pre** Fixtures under `tests/gates/fixtures/layer-lint/qaction-*/`, **one per spelling**:
`new QAction(...)`, a stack `QAction a(...)`, `menu->addAction("Literal", ...)`,
`toolbar->addAction(icon, "Literal", ...)`, and — as a **negative control** — the legitimate
construction inside `ui/shell/ActionFactory.cpp` plus a comment mentioning `QAction` in prose.
**Steps** `ctest --preset x64-release -R gates.layer_lint.qaction`
**Expect** Every offending spelling fails with the named diagnostic; both controls pass.
**If it fails** **S1.** This is I-15 restated: a rule tested against one spelling is a rule that only
catches one spelling, and that finding cost Stage 0 its third S1.
**Result** Green. **Four spellings, not five** — the plan's list named a stack `QAction a(...)`, a
`new QAction`, `menu->addAction("…")` and `toolbar->addAction(icon, "…")`, and those are the four
distinct constructions that exist; the fifth entry in the sketch was the control. Both controls pass:
the legitimate construction in `ui/shell/ActionFactory.cpp`, and a file that discusses `QAction` in
prose at length and constructs none. The rule is `escapable: false` and has **two** allowed paths, the
second being `ui/shell/RawPageAction.h` — the subclass definition is the one other place that must
name `QAction`, and pretending otherwise would have meant a waiver on the rule that is supposed not to
have any. The `gates` preset went 59 → **65**.

**A-13 · `i18n-lint` fails on an unwrapped literal, in every spelling**
☑ **Pre** Fixtures covering: a raw `"…"` passed to `setText`, a `QStringLiteral("…")`, a `u"…"`, a
literal built by `+` concatenation, a literal passed straight to `setWindowTitle`, and **negative
controls**: a literal inside a comment, a literal that is an object name, and a properly-wrapped
`tr("…")`.
**Steps** `ctest --preset x64-release -R gates.i18n_lint.literal`
**Expect** Every offender fails with its diagnostic and its file:line; every control passes.
**If it fails** **S1** if a real spelling passes clean.

**A-14 · `i18n-lint` fails on a key missing from a shipped locale**
☑ **Pre** A fixture where `rawpage_en.ts` has a key `rawpage_zh_TW.ts` lacks.
**Expect** Fails naming the key and the locale. **If it fails** S1.

**A-15 · A locale marked `"shipped": false` is not checked — and says so**
☑ **Goal** D1-14. `zh-CN` and `ja` are declared and deliberately unshipped; the gate must skip them
**with a named reason**, not silently.
**Steps** `i18n-lint --explain`
**Expect** The output lists `zh-CN` and `ja` as declared-not-shipped, naming Stage 15.
**If it fails** S2 — a silent skip is indistinguishable from a forgotten locale, which is exactly the
distinction D1-14 exists to keep visible.

**A-16 · An `ActionId` with no `action.*` catalog entry fails**
☑ **Expect** Fails naming the id. **If it fails** S2 — an unlabelled action renders as a blank button.

**A-17 · The waiver census counts applied waivers only**
☑ **Goal** Stage 0's I-20, transplanted. A waiver marker inside documentation must not be counted.
**Pre** Fixtures: a real waiver on a code line; a marker quoted in a comment; a marker for an unknown
kind; a marker on a line with no code.
**Steps** `i18n-lint --explain` on the real tree and on the fixture tree.
**Expect** The real tree's census matches the number of waivers actually applied in product code, and
the three non-waivers are not counted. **If it fails** S3 — but the census is how anyone notices
waivers creeping from a handful to a hundred, so an untrustworthy number defeats the escapability.

**A-18 · `license-audit` still clean; no new dependency**
☑ **Steps** As Stage 0's A-12. **Expect** No SPDX identifier outside the allowlist; `Qt6::Charts`
still absent from the link closure. **If it fails** S1 (license).

**A-19 · `THIRD-PARTY.md` is unchanged by this stage**
☑ **Goal** Stage 1 adds no third-party dependency. **Steps** `git diff --exit-code THIRD-PARTY.md`
after a full `checks` run. **Expect** No diff. **If it fails** S3 unless a dependency really was
added — in which case it needs a decision, not a diff.

### 5.3 The action layer — `tst_actionregistry`

**A-20 · Register, dispatch, enablement**
☑ **Steps** `ctest --preset x64-debug -R tst_actionregistry`
**Expect** Green. **If it fails** S1.

**A-21 · An unknown id returns an `Error`, does not assert, does not no-op**
☑ **Goal** Plan §5.1. The batch runner will dispatch ids from a user's file.
**Expect** `dispatch(unknown)` returns an error `Result`. **If it fails** **S1** — a silent no-op
becomes a batch run that reports success and did nothing, discovered at Stage 14.

**A-22 · Disabled and unknown are *distinct* error codes**
☑ **Expect** Two different codes, both with `tr()`-able message keys. **If it fails** S2 — the CLI
cannot tell a user "that does not exist" from "that is not available right now".

**A-23 · Double registration is an error, not an overwrite**
☑ **Expect** The second `register_` returns an error and the first handler survives.
**If it fails** S2 — two stages claiming one id should fail loudly at startup, not race.

**A-24 · Every registered id has a label key, and every declared id has a default context**
☑ **Goal** Generated from `ActionId.def` (D1-6), so this is really a test that the generator and the
table agree. **Expect** No id with an empty label key. **If it fails** S2.

**A-25 · The declared-versus-registered split is reported, not hidden** — ❌ **NOT RUN. The switch it
names does not exist.** *(T15)*
☐ **Goal** **360** declared and **22** registered as of 2026-08-31 (358/19 at T14; I-42 registered one and I-43 declared and registered two). *(Both figures in this case were wrong
until T15: it said "327 declared, ~60–80 registered" — the first a stale count, the second the plan's
original forecast, never re-measured. The real gap is more than twice as wide as the case claimed.)*
The gap must be *visible*.
**Steps** ~~`rawpage.exe --list-actions`~~ — **there is no `--list-actions` switch.** The parser's
vocabulary is `--version`, `--print-dpi-awareness`, `--print-args`, `--crash-test`, `--lang`,
`--dump-ui-strings` and `--help`, and nothing in `app/CommandLine.cpp` has ever recognised this one.
**Expect** Every id listed with its state: registered, or declared-with-owning-stage.
**If it fails** S3 — but this list is what stops a later stage silently forgetting to register
something it declared.
**Result — the case cannot run, and that is the finding (I-32).** The *property* is not unverified:
`--dump-ui-strings` proves all 360 ids resolve to a catalog label, `tst_actionregistry` proves an
unregistered id is refused with a distinct code, and the ribbon renders the split as disabled buttons.
What is missing is the **census in one place**, which is the half that stops a later stage silently
forgetting to register what it declared — and that gap grows with every stage. **Carried to Stage 2**
rather than closed here: adding a CLI verb belongs with the vocabulary `Features.md` §9.6 owns, and
inventing one at T15 to make a checkbox tickable is the shape of thing this guide exists to catch.

**A-26 · A renamed `ActionId` still resolves through its alias — D1-16** — ❌ **NOT RUN. The mechanism
exists and has never been exercised.** *(T15)*
☐ **Goal** Saved shortcuts and QAT entries key off the *string* id, so a rename in a later stage
must not silently discard a user's customization.
**Pre** A fixture with `RAWPAGE_ACTION_ALIAS("old.string.id", NewEnumerator)`. **There is no such
fixture, and `ActionId.def` declares zero alias rows.** The macro is implemented — `ActionId.h`
defines it twice, once to count and once to build the table — so the mechanism is real code that
nothing has ever run.
**Result — not run, and recorded rather than quietly skipped (I-32).** Stage 1 renames nothing, so
there is no alias to write and the case has had no subject; that is why it was never reached. But
D1-16's whole argument is that the *first* rename must not be the first exercise of the path, and by
that argument an untested alias table is exactly as useful as no alias table. This is I-15's lesson
in its purest form: a mechanism nobody has seen work. **Carried to Stage 2**, which should plant a
throwaway alias row and a fixture rather than wait for a real rename in Stage 7.
**Steps** Write a `shortcuts/` entry and a QAT entry under the **old** string, then load.
**Expect** Both resolve to `NewEnumerator`; the alias is reported by `--list-actions`; **no alias row
is ever removed** — the fixture asserts that deleting one breaks the load, which is the property that
keeps aliases permanent.
**If it fails** S2 — a rename that silently drops customizations is discovered by a user, not by us.

### 5.4 Key bindings — `tst_keybindings`

**A-30 · The §8 tables load, row for row**
☑ **Goal** D1-9. The test parses `Features.md` §8 and asserts the store matches every row.
**Steps** `ctest --preset x64-debug -R tst_keybindings`
**Expect** Green, and the test reports the number of rows parsed per context. On a parse failure it
must name **the row it could not parse**, not just fail.
**If it fails** S2 if the store is wrong; **S3 if the parser is wrong** — and the distinction matters,
so the failure output must make it obvious which.

**A-31 · The twelve Write-mode collisions — the figure the document set quotes**
☑ **Goal** [`Features.md` §8](./Features.md#what-the-twelve-deliberate-collisions-counts) scopes
"twelve" to Write mode. **Stage 13's exit criteria depend on this number**, so it is asserted here,
at the stage that builds the binding store, rather than discovered eleven stages later.
**Expect** The census reports exactly **12**: **8** overriding Viewer — `Ctrl+B` `Ctrl+E` `Ctrl+L`
`Ctrl+R` `Ctrl+U` `Ctrl+=` `Ctrl+Shift+=` `Ctrl+Shift+N` — and **4** overriding Global — `Ctrl+D`
`Ctrl+K` `Ctrl+Shift+A` `Ctrl+Shift+L`. Zero overriding Common.
**If it fails** S2, and the failure must say **which side moved** — a §8 edit, or the census.

**A-32 · The wider census — the two numbers that are *not* "twelve"**
☑ **Goal** Keep the three counts distinct. Conflating them is exactly what made the documented
figure look wrong before I-01 was resolved.
**Expect** **Class 1** — any context redefining a `Global` chord — exactly **6**: `Ctrl+D`, `Ctrl+K`,
`F11`, `Ctrl+Shift+L`, `Ctrl+Shift+A`, `Ctrl+Shift+G`. **Class 2** — a context overriding a `Common`
chord — exactly **9**: `Ctrl+X`, `Ctrl+C`, `Ctrl+V`, `Ctrl+A`, `Delete`, `Backspace`, `Esc`, `F2`,
`Ctrl+F`. §8's Common heading permits class 2 explicitly, so those are **not** shadows and
`shadows()` must not report them.
**If it fails** S2 — a shadow report that includes the permitted overrides is noise, and a noisy
report gets ignored.

**A-33 · Each class-1 collision resolves to the right id per context**
☑ **Goal** The one thing the whole `KeyContext` model exists for.
**Expect** `Ctrl+K` → Preferences in `Viewer`, → Insert-hyperlink in `WriteMode`. `F11` → enter in
`Global`, → leave in `FullScreen`. And so on for all six **class-1** chords (A-32) **and all twelve
Write-mode chords** (A-31). **If it fails** **S1** — this is the model.

**A-34 · Resolution order is innermost, then Common, then Global**
☑ **Expect** A chord defined in all three resolves to the innermost.
**If it fails** S1.

**A-35 · A remap persists and takes effect**
☑ **Expect** Round-trips through `shortcuts/<context>/<action>`; the new chord resolves and the old
one stops. **If it fails** S2.

**A-36 · A remap that shadows a Global chord is reported, not forbidden**
☑ **Goal** §8 ships six class-1 collisions by design; forbidding shadowing would make the specified
defaults illegal. **Expect** `shadows()` lists it; the remap succeeds. **If it fails** S2.

**A-37 · A shortcut key for a removed `ActionId` is dropped with a log line**
☑ **Goal** D1-10's family validator. **Expect** Dropped at load, one log line, no startup failure.
**If it fails** S3.

### 5.5 Settings — `tst_settings`, `tst_recentfiles`

**A-40 · Typed round-trip and defaults**
☑ **Steps** `ctest --preset x64-debug -R tst_settings` **Expect** Green. **If it fails** S1.

**A-41 · An undeclared key does not compile**
☑ **Goal** D1-10 — the guarantee that stops settings sprawl across sixteen stages.
**Steps** A negative-compile fixture (`try_compile`) naming a key that does not exist.
**Expect** Compilation **fails**. **If it fails** **S1** — if there is any way to name a key that was
never declared, the schema is a convention, not a guarantee, and it will be gone by Stage 6.

**A-42 · `changed(Key)` fires only on an actual change**
☑ **Expect** `set` to the current value emits nothing. **If it fails** S2 — from Stage 2 this signal
invalidates the render cache, and the resulting stutter will be blamed on the renderer.

**A-43 · Out-of-range values are clamped and logged, not rejected**
☑ **Steps** Hand-edit `settings.ini` to `general/uiFontScale=9.0`; launch.
**Expect** Starts, clamps to 2.0, logs it. **If it fails** S2 — a hand-edited INI that prevents
startup is a support call with no diagnosis path.

**A-44 · A malformed settings file does not prevent startup**
☑ **Steps** Truncate `settings.ini` mid-line; launch. **Expect** Starts on defaults with a log line.
**If it fails** S2.

**A-45 · Logging keys take effect, and document text never appears above debug**
☑ **Goal** `core/Log.h`'s standing rule, now that there is something to log.
**Expect** At `Info`, no file path and no document text in the log. **If it fails** **S1 (privacy).**

**A-46 · Recent files: note, pin, evict, clear**
☑ **Expect** `documents/recentCount` bounds the list; a **pinned** entry survives eviction; `clear()`
leaves pins per the specified semantics. **If it fails** S3.

**A-47 · Schema version: migration up, and no rewrite on downgrade — D1-16**
☑ **Goal** The three persisted vocabularies this stage creates must survive a later version changing
one of them. This is the half that is cheap now and a data-loss bug later.
**Pre** Three fixture `settings.ini` files: one at the current `meta/schemaVersion`, one at an older
version, one at a **newer** version carrying a key we do not know.
**Steps** Load each; inspect the file afterwards.
**Expect** Current loads unchanged. Older runs the ordered migration chain and is rewritten at the
current version. **Newer is read for what we understand and left byte-identical on disk** — the
unknown key is still there, and a log line says why.
**If it fails** S3 for a failed migration; **S2 for the downgrade case** — an older build that
rewrites a newer build's settings destroys them during a rollback, which is precisely when a user can
least afford it.

### 5.6 Shell widgets — `rawpage_ui_tests` under `offscreen`

All of §5.6 runs with `QT_QPA_PLATFORM=offscreen` (D1-4). **None of it can judge appearance** — that
is §6's job, and a case here that claims to have checked a *look* is a defect in this guide.

**A-50 · The ribbon builds entirely from `res/ribbon.json`** — ✅ **2026-08-24 at T10**
☑ **Steps** `ctest --preset x64-release -R "^ui\." --output-on-failure`
**Expect** Every tab, group, and control in the JSON is present in the widget tree, and **no control
exists that the JSON did not declare**. **If it fails** S2.
**Result** Green over **9 tabs, 49 groups, 192 controls**. Both directions are asserted, and the
second is the one that actually protects D1-8: "everything in the JSON is on screen" is equally
satisfied by a builder that *also* hard-codes three buttons of its own, and the moment one exists,
adding a command stops being a JSON line while the test stays green. The count of `RawPageAction`
children of the ribbon is compared against the spec's own control count, and every one of them is
checked to be an id the spec named.

**A-51 · An unknown action id in `ribbon.json` fails the build** — ✅ **2026-08-24 at T10**
☑ **Goal** D1-8. **Steps** Build with a deliberately misspelled id.
**Expect** **The build fails**, naming the id and the JSON line. Not a warning; not a missing button.
**If it fails** S2 — the moment a typo yields a silently missing button, the next person writes widget
code and the declarative ribbon is dead weight.
**Result** Verified by planting `"file.saevAs"` for `"file.saveAs"` and building. Actual output:

```
[1/1] ribbon-validate res/ribbon.json
FAILED: bin/res/ribbon.json
res/ribbon.json:44: unknown action id "file.saevAs" in group "home.file"
                    -- it is not declared in core/cmd/ActionId.def
ribbon-validate: 1 problem(s) in ribbon.json -- the ribbon is built entirely from this
                 file, so a bad row here is a missing button with no other symptom
ninja: build stopped: subcommand failed.
```

The id, the group and the line, and **the resource is not copied** — so there is no stale valid copy
beside the executable to mask it. The duplicate-placement and empty-group halves of D1-8, plus the
line-number resolution, are additionally pinned as unit cases against the same parser, which is the
point of `RibbonModel` being one parser rather than two: the build-time verdict and the runtime one
cannot disagree.

**A-52 · Theme tokens resolve for light, dark, and follow-system** — ✅ **2026-08-23 at T9**
☑ **Steps** `ctest --preset x64-release -R "^ui\.(every|light|the requested|the UI)"`
**Expect** Every declared token has a value in each mode; no hard-coded colour in a widget.
**If it fails** S3. *(High-contrast deference was withdrawn 2026-08-23 with D1-15, so there is no third mode to resolve.)*
**Result** Green over all **eleven** tokens in both modes. Four things are asserted rather than one,
because "every token resolves" is satisfied by a theme that returns the same colour twice: the count
is checked against `kThemeTokenCount` (so a token added to the enum and forgotten in the test cannot
hide), every token is valid and fully opaque, the four load-bearing tokens **differ** between modes,
and the **direction** is right — dark mode's window is darker than its text and light mode's is
lighter, which is the reversal an equality check cannot see. `FollowSystem` is additionally asserted
to be a *request* that resolves, never a palette of its own. Completeness itself is held by the
compiler, not by this case: `colorFor()` is one switch with no `default`, under `ui/`'s `-Werror`.

**A-53 · `activeContext()` over synthetic focus chains — `tst_context`** — ✅ **2026-08-24 at T13**
☑ **Goal** D1-5. Parented, never-shown widget trees; the innermost widget declaring a context wins;
a chain declaring none falls back to `Global`.
**Steps** `ctest --preset x64-release -R "^ui\." --output-on-failure`
**Expect** Green over every `KeyContext` value, plus a nested case (Comments pane inside a Viewer)
and a detached-widget case. **If it fails** **S1** — this function is what makes one chord mean two
things, and every collision case above is downstream of it.
**Result** Green, as **22 cases** rather than the three the goal names, because the goal describes
the focus walk and T13 has three separable parts that fail differently. The walk itself is asserted
**over the enum** — every `KeyContext` a widget may declare, so a context added to `KeyContext.h` and
forgotten here cannot look tested — plus the nested Comments-in-a-Viewer case, the detached widget,
the empty chain, a `Common` declaration **refused** rather than stored (it is a resolution layer, not
a place the focus can be), and an unreadable property falling through to the parent instead of
asserting, since that value arrives from data.
**Two things the case as written did not ask for, and both earn their place.** The **conversion
census**: every one of §8's 248 rows is turned back into a key event and read again, so a chord whose
key the boundary cannot name is a failure here rather than a binding that is loaded, censused,
printed by the tool finder — and dead. And the **resolution sweep** that is the T13 exit criterion:
each dispatched Global row is registered with a recording handler and pressed, and the id that
arrives is compared with the row's own, so a resolution returning the right *kind* of answer for the
wrong action fails. A second sweep does the same for every context's own rows, which asserts the
*order* rather than describing it.
**And the end-to-end half, on a real shown window:** `F4` toggles the sidebar and comes back
accepted; `Ctrl+S` — declared, unregistered until Stage 3 — comes back **unaccepted**, because
eating the **339** chords Stage 1 cannot service would make the shell swallow them for twelve stages;
a `QLineEdit` keeps `Ctrl+A` while `F4` still reaches the shell from inside it; and a chord typed
into a second window acts on that window only.
**What is NOT asserted, stated rather than implied:** no widget in the Stage 1 shell declares a
context, so `activeContext()` answers `Global` throughout the product today. The per-context tables
are exercised against synthetic chains and against the store, never against a real surface — the
Viewer's is Stage 2, the panes' Stage 4, Write mode's Stage 13. Declaring `Viewer` on D1-2's stub to
make the mechanism look exercised would have been a stub claiming to be a page surface.

**A-54 · The no-direct-connection self-check catches a planted violation** — ✅ **2026-08-24 at T10**
☑ **Goal** D1-7(b). A self-check nobody has seen fail is a self-check nobody knows works.
**Steps** `ctest --preset x64-release -R "^ui\." --output-on-failure`
**Expect** Two cases: the real window passes; a fixture window with one bare `QAction` and one
`RawPageAction` carrying a second `connect` **fails, naming both**.
**If it fails** **S1** — and note the lint (A-12) cannot catch the second one, which is exactly why
there are two mechanisms.
**Result** Green, as seven cases rather than two, and the extra five are all things the first run
taught. The planted bare `QAction` and the planted second `connect` are each named separately, and a
third case asserts both are reported **together** rather than the first one only. The real
`MainWindow` is `show()`n — without that the check in `showEvent` would never run and the product
half of the case would be untested no matter how many actions the fixtures built.

**And showing it is what produced I-18 and I-20.** The first run against the real window reported the
sidebar as a bare `QAction` called "Panes" and every one of the 192 ribbon actions as having two
receivers where one was expected. Both were the check being wrong rather than the product: Qt
manufactures intrinsic actions nobody asked for, and placing an action on a widget legitimately adds
a receiver. The invariant is now *every receiver accounted for* — one from the factory plus at most
one per widget the action sits on — with the residual gap written down rather than smoothed over.

**A-55 · QAT customization round-trips** — ✅ **2026-08-24 at T11**
☑ **Expect** Add, reorder, move above/below, reset; serialize and reload identically.
**If it fails** S3. *(Surviving a real restart is M-11.)*
**Result** Green, in three parts. **The operations**: add, remove, reorder and reset each change the
row and each *refuses the no-op* — a duplicate add, a remove of something absent, a move to where it
already is — and the assertions read the BUTTONS off the layout rather than reading the command list
back to itself, so a mutation that stopped rebuilding the row would fail them. **The round trip**: a
window writes to `Settings` on every change (not at shutdown — a customization that only survives a
clean exit is one a crash takes with it, and this is the stage that built a crash handler), and a
second window on the same settings file restores it exactly, placement included. What is on disk is
**action string ids**, so the customization survives a row being inserted into `ActionId.def` and,
through D1-16's alias table, a rename in a later stage; an id that resolves to nothing is dropped and
**named** in the log. **The blob**: `UiCustomization` serializes and parses, and a file written by a
build with a higher `version` keeps its unknown members through a re-serialize — the downgrade half
of D1-16, which is the half that loses a user's layout during a rollback. A blob with no `version` is
an **error** rather than an empty customization, because "imported successfully, everything reset"
is indistinguishable from a reset the user asked for.
**One behaviour the case did not anticipate, and it needed a new API:** a toolbar the user emptied
and a profile that never customized one both read back as an empty list, and only one of them means
"restore the factory defaults". `Settings::isSet()` is the difference.

**A-56 · Tool finder ranking** — ✅ **2026-08-24 at T11**
☑ **Expect** An exact name ranks above a prefix, which ranks above a fuzzy subsequence; each result
carries its current chord from `KeyBindingStore`; a disabled action is shown as disabled rather than
hidden. **If it fails** S3.
**Result** Green, and asserted against `toolfinder::rank()` — a free function naming no widget, which
is what lets the ORDER be checked without a window at all. The ranking claim is asserted as a
property of the whole list (*the kinds never improve as you go down*) rather than as three named
rows, so a command being renamed for good reasons cannot break it; the one row the catalog does
guarantee — a label that IS the query — is asserted by name. A fourth kind sits between prefix and
subsequence: **word-start**, so "all" finds *Close All*, which is the case a person hits constantly
because they remember the noun and not the verb.
**The chord comes from the store and is asserted by taking one away:** rebinding `Ctrl+W` to another
command makes *Close*'s chord go empty, which is the behaviour that matters — binding a *second*
chord would leave `Ctrl+W` firing and the finder would rightly go on showing it.
**A declared-but-unregistered command ranks, appears and is marked**, and choosing it is a refusal
that says why rather than a dispatch that fails somewhere below.

**A-57 · A stub `DocumentWindow` holds no document state** — ✅ **2026-08-24 at T12**
☑ **Goal** D1-2's guard against calcification. **Expect** The class has no `Document`, no
`CommandJournal`, and no member Stage 2 would have to *remove* rather than fill in.
**If it fails** S3 now, S2 at Stage 2 — which is why it is checked now.
**Result** Green, **and the case is worth less than it reads**, which is worth saying plainly. What a
test can check is that the class adds no property and no method to `QWidget`'s meta-object, and that
everything it reports — its title, its tab text, the window title it produces — is *derived* from the
one path it was given rather than stored beside it. Both are asserted. "No member Stage 2 would have
to remove" is a claim about the **header**, and the header is where it is made; a test that implied
otherwise would be the more comfortable of the two options and the less honest one.

**A-58 · `Ctrl+O` does not sniff file contents** — ✅ **2026-08-24 at T12** *(acceptance half green at T9)*
☑ **Goal** D1-2. Deciding whether bytes are a PDF is `IDocumentBackend`'s job.
**Steps** Open a `.pdf` whose contents are plain text.
**Expect** A stub tab opens. No read of the file body anywhere in `ui/`.
**If it fails** S2 — a content check here pre-empts Stage 2's boundary, and boundaries are much harder
to reclaim than to hold.
**Result** ✅ **Closed at T12.** The *acceptance* half went green at T9: `ui.opening a file checks
the name and never reads the bytes` writes a file whose contents are `this is plain text, not a PDF
at all`, names it `.pdf`, and asserts `MainWindow::openFiles()` accepts it and notes it in the recent
list; a companion case asserts the two refusals — a path that does not exist and one that is not
named `.pdf` — reach the recent list not at all. **T12 supplied the missing half**: that same
plain-text file now opens a **stub tab**, with its name on the tab, its path in the tab's tooltip and
in the window title, and not one byte read. What is still not asserted is the literal **`Ctrl+O`
keypress**, because chords arrive at T13 — but the command that keypress will reach is registered and
dispatched here, so what T13 adds is the key, not the behaviour.

### 5.7 Single instance, command line, crash handler

**A-60 · A second launch forwards and exits 0** — ✅ **2026-08-23 at T8**
☑ **Steps** Launch `rawpage.exe`, then `rawpage.exe C:\path\to\a.pdf` from a second shell.
**Expect** The second process exits **0** quickly; the first opens a stub tab for the path.
**If it fails** S2. *(Raise-to-foreground is M-07 — offscreen has no foreground.)*
**Result** Green, and run with a path chosen to test more than the handoff: `測試-📄.pdf`. The
secondary logged *"secondary, forwarding to the running instance"* and exited **0**; the primary
logged *"open: accepted …/測試-📄.pdf"*. That path crossed the shell's command line,
`QCoreApplication::arguments()`, a length-prefixed named-pipe message and a queued invocation onto
the primary's UI thread, and came out **byte for byte identical** — which is I-04's rule proven
across three process boundaries rather than asserted at one. **A stub tab is T12**, so the primary
reports the accepted path in the status bar and the log instead; the case is re-run at T12 for that
half.

**A-61 · `general/singleInstance=false` gives two windows** — ✅ **2026-08-25 at T15**
☑ **Steps** Two halves, and the split is forced by a measurement rather than chosen.
`ctest --preset x64-release -R "A-61"` for the policy; `.\scripts\check-single-instance.ps1` for the
two processes.
**Expect** Two processes, two windows, neither forwarding. **If it fails** S3.
**Result** Green, both halves, and each was written so that it could fail.

*The policy half* is `tests/unit/tst_singleinstance.cpp`, five cases in the **headless** suite —
`app/SingleInstance.cpp` is compiled in the way `CommandLine.cpp` and `LogFile.cpp` already are, so
A-83 is untouched. Against a bus that records every call, `claim(false)` leaves `claims == 0`: not
"we decided not to forward" but *no OS object was created at all*, which is the assertion that
matters, because a claim that happens and is then discarded still leaves a `Local\` mutex for the
next launch to find. It runs on every push, on Windows and under `portable-asan`.

*The two-process half* is a script, and **why** it is a script is itself a T15 measurement: Qt
resolves `QStandardPaths::AppDataLocation` through `SHGetKnownFolderPath`, so setting `APPDATA` for
a child process does **not** move the settings file — verified by pointing it at a scratch directory
and watching `rawpage.exe` log `C:/Users/…/AppData/Roaming/RawPage/settings.ini` anyway.
`CompositionRoot::Options::settingsPath` exists but `main.cpp` exposes no switch for it, and adding
a CLI verb at T15 would extend the vocabulary `Features.md` §9.6 owns for Stage 14. So the harness
edits the real file and restores it in a `finally` — which is exactly why it must not be a CTest
case that rewrites the settings of whoever ran the suite.

With `singleInstance=false`: **both processes were still alive** after the second launched, both
logged *"disabled by general/singleInstance"*, and neither called itself a secondary.
**With the control at `true`**: the first stayed and logged *"-- primary"*, the second logged
*"-- secondary"* and **exited 0**. The control is not decoration — without it, every assertion in
the `false` case is equally satisfied by a build whose single-instance code had been deleted
outright, which is convention 7 applied to a harness rather than to a fixture.

**A-62 · The mutex is `Local\`, not `Global\`** — ✅ **automated at T7**
☑ **Goal** D1-11's per-session scope. **Steps** `ctest --preset x64-release -R platform.the_mutex`
(was: inspect the named object).
**Expect** `Local\`. **If it fails** S3 — a `Global\` mutex means the second user on a machine cannot
run the app at all, which is a support incident nobody will diagnose quickly.
**Note added at T7:** the pipe needs the same scrutiny and does not get it from the same mechanism.
`Local\` scopes a mutex and **not** a named pipe, so the pipe name carries the session id
explicitly; `platform.the_pipe_name_carries_the_session_id` asserts it.

**A-67 · The single-instance bus delivers a handoff** — *new at T7*
☑ **Goal** The mechanical half of A-60, runnable before `rawpage.exe` can forward anything (T8).
**Steps** `ctest --preset x64-release -R "^platform\."`
**Expect** 19/19. The claim/forward/deliver round trip runs single-process — `CreateMutexW` returns
`ERROR_ALREADY_EXISTS` for a name the same process holds, so the second bus is a genuine secondary —
and a non-ASCII path round-trips byte for byte.
**If it fails** **S2.** *(Cross-process foreground activation has no offscreen equivalent and stays
manual case M-07.)*

**A-63 · `tst_commandline` — parse only** — ✅ **2026-08-23 at T8**
☑ **Steps** `ctest --preset x64-release -R tst_commandline` plus the two compile cases
`ctest --preset x64-release -R "unit.commandline.compile"`.
**Expect** Green: an open-with-file path, `--lang <locale>`, `--version`, an unknown flag (error
with a documented exit code), and an empty argv. **If it fails** S2.
**Result** Green: ten cases covering an empty list, a bare program name, ordered file paths left
exactly as written, `--lang` in both spellings, a missing value, an unknown option (exit code **2**,
verified against the real executable through `Start-Process` — PowerShell does not wait on a
GUI-subsystem binary, so a bare `$LASTEXITCODE` reads the previous command's), every diagnostic
switch, `--help` in three spellings with the text checked against the parser's own vocabulary, and
`--` as the literal separator.
**Added at T8, and this is the case I-04 asked for:** an emoji path and a Han path each survive the
parser byte for byte, compared through `toUtf8()` so that two strings mangled by the same transcode
could not pass by matching each other.

**A-64 · The parser links without the executable** — ✅ **2026-08-23 at T8**
☑ **Goal** Plan §5.7. It lives in the headless suite; if that stops being possible, the parser has
grown a dependency. **Expect** `rawpage_tests` builds and `rawpage_assert_headless()` still passes.
**If it fails** S2 — and the finding is about the parser, not the test.
**Result** Green. `tests/unit` compiles `app/CommandLine.cpp` **in** rather than linking the
executable, and adds no `target_link_libraries` entry — so `rawpage_assert_headless()` still walks a
closure of `rawpage_core` + `rawpage_pdf` and would fail **configure**, not a test, the day the
parser needed a widget or an OS handle. This case is therefore enforced by the build rather than
observed by an assertion, which is the stronger form.

**A-65 · A forced crash writes a symbolizable minidump** — ✅ **re-run 2026-08-25 at T15**
☑ **Goal** D1-13, the whole point of building this path now rather than at Stage 15.
**Steps** `rawpage.exe --crash-test` (debug builds only), then
`scripts\symbolize.ps1 -Dump <path> -Binary .\build\x64-debug\bin\rawpage.exe`.
*(The parameter is `-Binary`, not `-Exe`; and passing it **matters** — see the result.)*
**Expect** A `.dmp` in `%LOCALAPPDATA%\RawPage\crashes\`, and the script resolves the faulting frame to
a **function name**, not a bare address.
**If it fails** **S2** — a crash handler that has never symbolized a real dump is a crash handler that
will not work the first time it matters, and the first time is Stage 2.
**Result** Green, and the re-run produced the refusal as well as the answer, which is the better
demonstration. The debug build faulted with `0xC0000005` and wrote
`rawpage-20260825-070048-046172.dmp`. Symbolizing it **without** `-Binary` refused:

```
  build id (dump)  427A01F2D54A51E9732571745CCE813F1
  build id (exe)   7D0B1EBE00B4F80361EFCE0079DB86611
  Build ids differ: the dump did not come from this binary. Symbolizing anyway would print
  function names from the wrong build -- plausible, confident and wrong, which is worse than no
  answer.
```

That is D1-13's refusal working: the script defaults to the *release* binary, and a debug dump does
not match it. Pointed at the right binary, the ids agree and it answers:

```
  Faulting frame
    rawpage::app::CrashHandler::forceCrashForTesting()
        at .../app/CrashHandler.cpp:287
```

**The steps as originally written would have failed** — they named `-BuildId <id>`, which resolves
nothing on its own. Corrected above.

**A-66 · The crash path runs its last rites** — ✅ **re-run 2026-08-25 at T15**
☑ **Goal** The recovery-journal flush hook. `RecoveryJournal` is Stage 3, so Stage 1 installs the hook
with a no-op sink.
**Expect** The hook is invoked before the dump is written, and the fact that it is a no-op is
**recorded in the log**, not implied by silence. **If it fails** S3.
**Result** Green. Every launch logs it in as many words, before anything else can go wrong:
`CrashHandler installed; buildId 7D0B1EBE… ; lastRites no-op (RecoveryJournal arrives at Stage 3)`.

### 5.8 Internationalization

**A-70 · `zh-TW` is key-complete against `en`**
☑ **Steps** `ctest --preset x64-release -R gates.i18n_lint` on the real tree.
**Expect** Zero missing keys. **If it fails** S2.

**A-71 · No unwrapped literal in the real tree**
☑ **Expect** Clean, with the waiver census printed and small. **If it fails** S2, and each waiver
needs a reason a person wrote.

**A-72 · `--lang zh-TW` actually loads the catalog** — ✅ **T8/T9, re-measured 2026-08-25 at T15**
☑ **Steps** Under `offscreen`, launch with `--lang zh-TW --dump-ui-strings`.
**Expect** A sampled set of strings differs from the `en` run, and **no string is the untranslated
English fallback**. This is a mechanical check of *loading*; whether the Chinese is any **good** is
M-06, and this case must not be reported as covering that.
**If it fails** S2.
**Result — re-measured at T15, because the figures below had been the T9 ones for five tasks.**
Green: **515 strings per locale** (380 at T9), and *not* a sample — the dump is generated from
`ui/shell/ShellKeys.def` and `ActionId.def` themselves, so a key that reaches the screen and not the
dump is impossible rather than unlikely. **510 differ; 0 render a raw catalog key.**

The **five** identical rows are named rather than rounded away, and each is deliberate:

| Key | Why it is the same in both locales |
|---|---|
| `shell.window.title` | the product name |
| `shell.window.titleWithDocument` | a format string whose only literal part is the product name |
| `shell.placeholder.heading` | the product name |
| `shell.status.pageCountUnknown` | an em dash, which zh-TW currently sets the same way |
| `ribbon.overflow.glyph` | the chevron itself — a glyph, not a word |

All five are data a reviewer can change at M-06, which is the point of their being catalog entries at
all. *(T9 recorded three; the fourth and fifth arrived with T12's document-title format and T10's
overflow chevron and were never added here — the same drift as **I-32**.)*
**Measured rather than assumed, and it corrected a belief:** `lrelease` *keeps* a message marked
`type="unfinished"` as long as it has text, so the 28 provisional zh-TW terms do reach the running
product and are exactly what M-06 reviews. Only an **empty** translation is dropped. `en` is still
installed underneath every locale, for the day a key is added to `en` and not yet to zh-TW.
**This case says nothing about whether the Chinese is any good.**

### 5.9 CI, portable, and the one benchmark

**A-80 · CI green on all three jobs** — ✅ **2026-08-25 at T15, and the `windows` job ran for the
first time in the project's history**
☑ **Expect** `gates`, `windows`, `portable-asan` all green; the new counts recorded here and in the
status block. **If it fails** S1.

**Result — run 32785493072, four jobs, all green:**

| Job | Result | Counts |
|---|---|---|
| `gates` (ubuntu, no Qt, no vcpkg) | ✅ | **52/52** — the preset's 66 minus the 14 Windows-only `wrong_compiler` fixtures (Stage 0's I-19) |
| `windows` (self-hosted, Qt 6.11.1 / MinGW 13.1.0) | ✅ **first run ever** | **334/334**, registering `unit.` 144 · `ui.` 104 · `platform.` 19 · `bench.` 1 |
| `portable-asan` (ubuntu, ASan+UBSan) | ✅ | **196/196**, and zero `ui.`/`platform.`/`bench.` cases — the portable subset asserted rather than assumed |
| `coverage-report` | ✅ | Reported Windows coverage obtained |

**A-85's figure, re-measured on the runner rather than on this machine:** switch p95 0.343 ms, layout
0.000 ms, paint 0.140 ms, **TOTAL p95 0.476 ms** — against 0.471 ms locally, a 1 % difference across
two machines, which is what a stable measurement looks like. The `windows` job also confirmed the
linkage check (`msvcrt.dll` present, `ucrtbase.dll` and `Qt6Charts` absent) and ran the gates **against
the real configuration**, which is the only place `qt-modules.txt` and `compiler-plugins.txt` exist —
so the Qt Charts and GCC-plugin checks have now had teeth for the first time as well.

**What this run actually proves, beyond the counts.** Three quarters of the product's test surface —
the widget suite, the OS layer, the toolchain pin and the real-configuration gates — had never once
been exercised by CI. It now has been, and it was green on the first attempt. The reason it had not
is **I-30**, and the reason nobody noticed is that a skipped job renders as a grey tick beside two
green ones: the `coverage-report` job exists so that can never again be read as coverage.

**History.** ✅ `gates` **45/45** and ✅ `portable-asan` **177/177** on run 32649932954, the first CI run
to reach Stage 1's code at T9's counts. (The Linux `gates` job runs 45 of the preset's 66 — the
`wrong_compiler` fixtures are Windows-only, Stage 0's I-19.) The `windows` job had **never run**: the
workflow gates it on `vars.RAWPAGE_HAS_WINDOWS_RUNNER == 'true'`, so it was *skipped* rather than
failing, and a skipped job renders as a grey tick beside two green ones.

**T15 found out why, and it was not the reason everyone assumed.** The plan and this guide both read
the unset variable as *"no self-hosted runner has been registered yet"*, and that had been false for a
week: the repository has had a runner named `recto-dev-win`, labelled `self-hosted / Windows / X64 /
qt6111`, **online since 2026-08-17**. What was missing was a name. The one repository variable that
existed was `RECTO_HAS_WINDOWS_RUNNER=true` — a different product's spelling — and the workflow reads
`RAWPAGE_HAS_WINDOWS_RUNNER`. Two artefacts that had to agree, in different systems, with nothing
checking that they did. Logged as **I-30**.

`RAWPAGE_HAS_WINDOWS_RUNNER=true` was set on 2026-08-25 with the user's authorization, and the next
push produced the run recorded above.

**And the skip no longer reads as a pass.** A fourth job, `coverage-report`, runs on every push
whatever the others do, and when `RAWPAGE_HAS_WINDOWS_RUNNER` is not `true` it writes into the run
summary exactly what had no coverage — `ui.*`, `platform.*`, `bench.*`, the `wrong_compiler` fixtures,
the real-configuration gate run (the only place `qt-modules.txt` and `compiler-plugins.txt` exist, so
the Qt Charts and GCC-plugin checks have teeth nowhere else) and the `msvcrt`/`ucrtbase` linkage check
— and raises a workflow warning. It deliberately does **not** fail: keeping CI green without a runner
is the recorded decision, and a job that went red for missing infrastructure teaches everyone to
ignore a red build. This is the guide's own instruction to T15 — *record the deliberate absence rather
than let a skipped job read as a passing one* — discharged by the machine on every run instead of by a
sentence in a document that ages.

**A-81 · `checks` still runs before any compile in CI** — ✅ **2026-08-25 at T15**
☑ **Steps** Read the workflow log ordering. **Expect** The gate step precedes the build step in every
job that has both. **If it fails** S2.
**Result** Green, and it holds structurally rather than by step order alone. The `gates` job compiles
only the gate executables — which link nothing but the standard library (D0-1) — and both other jobs
declare `needs: gates`, so a layering or licence violation stops everything before a runner installs a
toolchain. Inside the `windows` job the order is *Configure → Gates against the real configuration →
Build*, and that second gate run is not redundant: `qt-modules.txt` and `compiler-plugins.txt` are
written at configure time from the actual link closure and the actual compiler, so the Qt Charts check
and the GCC-plugin check have teeth in that job and nowhere else.

**A-82 · `rawpage_ui_tests` runs on Windows and *only* on Windows** — ✅ **2026-08-25 at T15**
☑ **Goal** D1-4. **Expect** Present in the `windows` job, absent from `gates` and `portable-asan`.
**If it fails** S2 — adding Qt Widgets to a Linux job to chase coverage quietly redefines what
`portable-asan` proves, and nobody would notice.
**Result** Green, and **asserted in all three jobs rather than described in a CMake comment**, which
is what T15's *"`tests/ui/` into the CI `windows` job"* actually amounts to. `ctest --preset
x64-release` already ran the widget suite; what it could not do was notice its **absence**, because a
target that failed to build or a discovery that yielded nothing subtracts cases from a run that still
reports *"100% tests passed"*. So each job now counts what it registered: the `windows` job fails if
any of `unit.`, `ui.`, `platform.` or `bench.` is **empty** (locally 144 / 104 / 19 / 1), and `gates`
and `portable-asan` each fail if any `ui.`/`platform.`/`bench.` case exists at all.

**The absence check was written wrong first, and how it was caught is the part worth recording.** Run
against a listing that genuinely contains 124 such cases, the first version of the pattern returned
**0** — it would have passed on a Linux runner with the entire widget suite leaked onto it. The cause
was mundane, but the lesson is convention 7's: a negative control has to be shown failing against real
positive input before it is trusted. It was, and it now reports 124.

**A-83 · The headless assertion still holds** — ✅ **2026-08-25 at T15**
☑ **Goal** The pressure D1-4 exists to resist. **Expect** `rawpage_tests` links neither `rawpage_ui` nor
`rawpage_platform`, directly or transitively. **If it fails** **S1.**
**Result** Green, and T15 applied the pressure rather than merely re-reading the assertion: A-61 needed
`app/SingleInstance` in the headless suite, and `SingleInstance` names `platform/platform.h`. The
tempting repair is one `target_link_libraries` line. Instead the translation unit is **compiled in**,
the way `CommandLine.cpp` and `LogFile.cpp` already are, because `platform.h` is an *interface* header
that includes no OS header — its own `layer-lint` layer entry says so. `rawpage_assert_headless()`
still resolves to `rawpage_core` + `rawpage_pdf` over the whole closure, and `portable-asan` runs those
five new cases on a machine where neither forbidden library exists at all.

**A-84 · Every prior stage's cases still pass** — ✅ **2026-08-25 at T15**
☑ **Steps** The whole Stage 0 gate suite. **Expect** Green at its Stage 0 counts plus the new i18n
cases. **If it fails** S1.
**Result** Green. The bare `gates` preset — **no Qt, no vcpkg** — is **66/66**, against Stage 0's 26.
Both Windows presets are **347/347** (328 at T14, 334 at T15): +5 from A-61's policy cases, +1 from
the benchmark, **+8 on 2026-08-30 from I-33 … I-40** and **+5 on 2026-08-31 from I-41 … I-45** —
I-45 added no case of its own, being a regression in I-44's fix; it is two assertions and a
control inside I-44's case — every one of those eight added by a
manual walk finding something in the product rather than by a task building one. I-39 added no case
of its own: it was found in the step I-37 created and is asserted inside that case, in both
directions.
`checks` is clean on three gates at **141 files / 933 includes**, `i18n-lint` at 78 files / 2 catalogs
/ **0 waivers**, `license-audit` clean at 17 components / 4 Qt modules.

**A-85 · R-9 — a ribbon tab switch fits in one frame** — ✅ **2026-08-25 at T15**
☑ **Goal** The one A3 criterion that is measured rather than judged.
**Steps** `ctest --preset x64-release -R bench.ribbon`, or the binary directly for the figures.
**Expect** p95 ≤ 16 ms. Report the actual figure, not a verdict.
**If it fails** S3 and it feeds A3's verdict as *go with named custom work* — a slow tab switch is a
painting problem with known fixes, not evidence that Qt Widgets cannot host a ribbon.

**Result — p95 = 0.471 ms against a 16.667 ms budget**, over the **shipped** 9-tab ribbon (49 groups,
192 controls) at 1600×200, 100 iterations round-robin across all nine tabs after 10 warmup.

**This is the second R-9 measurement, not the first, and the difference is the point.** T2's spike
reported p95 **0.28 ms** and that figure already sits in `Tech-Stack-Plan.md` §3.3 — but a spike's
ribbon is exactly as large as the spike's author made it, and R-9 is a claim about the *product*. This
run is the first over the shipped resource. It is **slower** than the spike's, by 0.19 ms, which is
what 192 real controls cost and is the number worth carrying forward:

| Phase | p50 | p95 | max |
|---|---|---|---|
| switch (page swap) | 0.321 ms | 0.330 ms | 0.360 ms |
| layout (collapse decision) | 0.000 ms | 0.000 ms | 0.000 ms |
| paint (raster, no compositor) | 0.121 ms | 0.143 ms | 0.144 ms |
| **TOTAL** | **0.442 ms** | **0.471 ms** | **0.504 ms** |

Three phases rather than one total, because a single opaque number cannot be acted on — and the
`layout` row reading 0.000 ms everywhere is exactly why the benchmark carries **its own control**. A
zero there has two explanations: collapse genuinely costs nothing on a tab switch, or `activate()` is
a no-op on a layout nothing marked dirty and the column is a dead instrument reading zero. So the run
ends with 20 resizes across the collapse threshold (900 ↔ 1600), which is what dirties it: **0.714 ms
p50, 0.783 ms p95.** The instrument works, so the zero is a *fact about the ribbon* — the collapse
decision belongs to resize, not to tab switch, which is precisely what makes a switch cheap.

**Caveat, printed with the figure rather than left for a reader to remember:** measured under the
offscreen platform, so this is layout and raster painting with no compositor and no swap chain. It is
a floor, not a ceiling. R-9's *"on the reference machine"* half is therefore still owed by a walk on a
real display, and it is the only part of A3 this number does not close.

**The first run of this benchmark reported 0.000 ms for every phase, and that was the benchmark's own
bug** — the three percentile calls were arguments to one `printf`, argument evaluation order is
unspecified, and the `std::move` on the last one ran first, leaving p50 and p95 reading a moved-from
vector. It was caught because a 192-control ribbon cannot repaint in under a microsecond, not because
anything failed. Logged as **I-29**.

### 5.10 The Preferences surface and the log pane — T14

Written when T14's code was, and the numbering jumps to A-90 on purpose: §5.6's block runs to A-58
and §5.7 starts at A-60, so a new subject gets a new block rather than renumbering cases that other
documents already cite. All of it runs under `QT_QPA_PLATFORM=offscreen` except A-93, which is
`core/` plus one `app/` translation unit and needs no display at all.

**A-90 · Every Preferences page is GENERATED from `SettingsSchema`** — ✅ **2026-08-25 at T14**
☑ **Goal** The T14 row's own requirement — *every page built from `SettingsSchema`, not
hand-laid-out*. This is the case the whole design turns on, and it is worth saying why it needs to
exist: a hand-written form for today's five General keys looks **exactly** like a generated one, and
the difference only shows up in Stage 9 when somebody adds a key and no control appears.
**Steps** `ctest --preset x64-release -R "^ui\." --output-on-failure`
**Expect** For every shipped page, the set of rows equals the set of declared keys in that group —
**both directions** — and each row's editor is the one its type asks for.
**If it fails** **S2.**
**Result** Green. The row set is compared against `SettingsSchema::allKeys()` filtered by group, so
a key added to `SettingsKeys.def` in a later stage either appears on its page or fails this case;
and the *reverse* direction is the half that actually protects the design, exactly as it is for the
ribbon at A-50 — "every declared key has a row" is equally satisfied by a page that also hard-codes
three rows of its own, and the moment one exists, adding a preference stops being a `.def` row while
the case stays green. The editor mapping is asserted per kind: a **combo** carrying the codec's own
tokens for `general/theme`, a **double spin box bounded by the schema's own range** (0.75–2.0) for
`general/uiFontScale`, a **check box** for a bool, a **spin box** bounded 1–100 for
`logging/retainFiles`, and **seven check boxes** for the `logging/subsystems` flag set. A separate
case asserts the `Internal` group has no page and that `meta/schemaVersion` and `ui/qatCommands`
appear on none — `SettingsKeys.def` rule 4, checked rather than trusted.

**A-91 · A change propagates, persists, and comes back the other way** — ✅ **2026-08-25 at T14**
☑ **Goal** Flow J, and the T14 row's *"changing a setting propagates"*.
**Steps** Same command.
**Expect** Driving the **widget** writes the setting; the setting reaches `settings.ini`; and a
change made elsewhere updates the widget. **If it fails** S2.
**Result** Green, in three parts, and the first part is deliberately driven through the control
rather than through `Settings::set` — a case that called the setter would assert that the settings
store works, which T5 already proved, while leaving *"is the control wired to it at all"* untested.
The round trip syncs and reloads from the same file, because a preference correct in memory and
absent from disk is a failure this stage has already had once (**I-14**). The reverse direction — a
value changed by something else refreshing the page — is the half nobody builds, and it carries a
second assertion on top: setting `general/uiFontScale` to 9.0 makes the control show **2.0**, so the
clamp of A-43 is visible on the one screen built to explain it rather than being a silent
disagreement between what the page says and what the product uses. `restoreDefaults()` resets its
own page's keys and provably leaves another page's alone.

**Every key on a shipped page has a label the catalog can resolve** is asserted in the same suite,
over the schema rather than over a list: the label key is derived from the path
(`prefs.setting.<group>.<key>`), so a key with no catalog entry renders as itself, and that is a
defect nobody would notice reviewing an English screenshot three stages later. The page names and
all twelve `KeyContext` names are checked the same way.

**A-92 · The Shortcuts page edits `KeyBindingStore` and shows shadows** — ✅ **2026-08-25 at T14**
☑ **Goal** The T14 row's *"a remap persists and takes effect"*, and the page half of D1-9's subject.
**Steps** Same command.
**Expect** A remap reaches the store **and** the settings file; the old chord stops working; a reset
gives it back; the store's one refusal is reported rather than swallowed; the class-1 shadows are
listed. **If it fails** S2.
**Result** Green. A remap of `file.preferences` from `Ctrl+K` to `Ctrl+Alt+P` is asserted three
ways — the live store answers to the new chord, a **second store built from the reloaded settings
file** answers to it too, and `Ctrl+K` resolves to nothing in either. That last assertion is the one
that found **I-26**: until T14 an override *added* a chord, so both keys fired and only one of them
could be persisted. Resetting the row is asserted through the surface rather than the store, which
is what exercises the awkward part — after a rebind, the row on screen is the *new* chord, so a
reset keyed on "whatever this row shows" would work here and silently fail on the freed default row.
The refusal case drives the `FormsFill` context, where §8's widget-native rows live: such a row is
**shown** (hiding it would make the page disagree with the document it is a view of) and cannot be
rebound, and remapping a real command *onto* its chord is refused.
**What this case learned by getting it wrong.** It first asserted the shadow report totalled **6**
and got **7**. Both numbers are right about different things: §8's class-1 census is six *chords*,
one of which is redefined in two different contexts, so the store returns seven *entries*. That is
the same conflation that made the documented figure look wrong at **I-01**, so the case now asserts
both — every entry the store reports reaches a page, and the distinct chords number six.

**A-93 · The log buffer and the rolling log file** — ✅ **2026-08-25 at T14**
☑ **Goal** `Features.md` §9.5's first bullet, and the reader `logging/retainFiles` has been waiting
for since T5 declared it.
**Steps** `ctest --preset x64-release -R "tst_logbuffer" --output-on-failure`
**Expect** The ring keeps the newest N and counts what fell off; a subscriber hears every record
until it unsubscribes; the file writes, rotates and retains; a directory that cannot be created is
**not fatal**. **If it fails** S3, except the last, which is **S2** — a full or read-only profile
must never prevent the application from starting.
**Result** Green. Two of the cases exist because of hazards rather than requirements: a handler that
**logs from inside a notification** must not deadlock the buffer it is logging into (a log pane
failing to do something is exactly the sort of thing that logs), and the clear operation
deliberately does **not** reset the discard count — that count answers *"how much of this session did
I never see"*, and a user who cleared the pane has not un-discarded anything. The unwritable case
points the directory at an existing **file**, which `mkpath` cannot turn into a directory on any
platform — a permissions path would have passed on the wrong machine.
**Verified against the real executable as well**, which is where **I-28** came from: the file is
written to `%LOCALAPPDATA%\RawPage\logs\`, retention holds, and the first line now names the build
id.

**A-94 · The log pane shows what was recorded, and only that** — ✅ **2026-08-25 at T14**
☑ **Goal** `Features.md` §9.5's in-app viewer with filtering and copy.
**Steps** `ctest --preset x64-release -R "^ui\." --output-on-failure`
**Expect** Records already in the buffer are there when the pane opens; a record logged afterwards
arrives; both filters narrow; the pane is built **hidden** and `pane.log` toggles it as a checkable
command. **If it fails** S3.
**Result** Green. The filter is asserted on **both** code paths — the rebuild and the live append —
because one rule implemented twice is a rule that gets forgotten once, and the symptom there is a
pane that respects the filter until the next log line lands. The level ordering is asserted by
direction rather than by count: `Error` is the *narrowest* setting, and a filter that read the
enum the other way would show everything at every setting and look like it worked. A last case
destroys a pane with a queued notification **still in flight** — the fifth door I-12's lesson has
come through, and the one where `~QObject` dropping its own posted events is what makes it safe.

---

## 6. Manual verification — you run this

Large, and that is correct for this stage rather than a failure of the guide. Everything below needs a
real screen, a real second monitor, a real input device, a real shell, or your reading of Chinese.
Anything here that *could* have been automated is a defect in this guide — say so and I will move it.

**The A3 verdict was assembled from M-01, M-02 and A-85** — ✅ **recorded 2026-08-23 as
`go, with named custom work`**, per criterion rather than as one overall impression, so a reader can
tell which work was named. M-08 contributed nothing: it was withdrawn with R-8 the same day, so the
verdict rests on **eight criteria rather than nine**, which is stated in the verdict itself rather
than left for someone to notice.

---

**M-01 · The ribbon is a real ribbon — A3's core (R-1, R-2, R-3, R-4, R-5, R-7)** ✅

> ✅ **Walked and signed off by the user, 2026-08-23**, against the P-4 reference. **R-1, R-2, R-3,
> R-4, R-5 and R-7 all met** — R-2 and R-3 as *met with named custom work*, which the §5.3 scale
> counts as a pass. The walk found **I-05**: `Tab` visited the groups in collapse-priority order
> rather than visual order. Fixed, negative-controlled, and the order half of R-8 automated as a
> result (**I-06**). The user's verdict, recorded — not mine.
- **Goal** Decide, against a reference and not against our own expectations, whether Qt Widgets host a
  ribbon a user of Right PDF would recognize as one.
- **Pre** P-4 — the reference application open on the same machine, same monitor, same scaling.
- **Steps**
  1. Open RawPage beside the reference. Compare a populated tab: group boundaries, the large/small
     button mix, the gallery, the baseline grid. **(R-1)**
  2. Drag the window narrower, slowly, through every group's collapse threshold and back, twenty
     times. Watch for a group that fails to collapse, collapses at the wrong width, flickers, or
     leaves the ribbon a different height than it started. **(R-2)**
  3. Narrow it until tabs overflow; use the chevron. **(R-3)**
  4. Trigger a contextual tab on and off ten times. Watch the ribbon height and the tab strip. **(R-4)**
  5. Press `Alt`. Navigate two levels deep by key tips only. `Esc` out. **(R-5)**
  6. `F9` to collapse to the tab strip and back; confirm the active tab survives. **(R-7)**
- **Expect** Each of R-1, R-2, R-3, R-4, R-5, R-7 recorded **separately** as met, met-with-custom-work,
  or not met. Step 2 is the one that decides this case — group collapse is the part of a ribbon that
  toolkits usually cannot do.
- **If it fails** **S1 against A3.** Name the criterion and describe what you saw; a criterion that
  needs a custom `QLayout` is *not* a failure and should be recorded as **go with named custom work**.
  The distinction is the whole verdict.

**M-02 · Mixed DPI, and a scale change while running (R-6)** ✅

> ✅ **Re-walked in full and signed off by the user, 2026-08-30.** All four steps, **step 3 and step 4
> included** — the mid-session scale change made in Windows Settings with the app running, and the
> log-out / log-in at a different primary scale. The half R-6 had never been judged on — the sidebar, a
> floating pane and Preferences, on each monitor in turn and dragged between them — behaved. **No
> findings, and therefore no §8 entry**; precondition **A-06** was re-checked first and still reports
> `PerMonitorAwareV2`, so the walk was reading the product rather than a mis-set awareness mode. This
> discharges the re-walk the two notes below scheduled, and M-02 is ✅ rather than 🟡.
>
> ⤵ *The two notes below record why this case stood amber from T2 to T14. Kept as breadcrumbs.*
>
> 🟡 **Walked and signed off by the user, 2026-08-23 — for the RIBBON, which is all that exists.**
> **R-6 is met** and A3's DPI question is answered. Recorded as 🟡 rather than ✅ only because step 2
> names *"the sidebar, a floating pane, and Preferences"*, and at T2 none of them exists — `SidebarDock`
> and `ThemeManager` arrive at **T9**, `LogPane` and `PreferencesDialog` at **T14**. **This case must be
> re-walked after T14** for its non-ribbon half. That is a scoping fact about what was on screen, not a
> qualification of the user's verdict: what could be tested passed.
>
> ✅ **Walkable in full since 2026-08-25 (T14).** Every widget step 2 names now exists — the sidebar
> and a floating pane since T9, Preferences and the log pane since T14 — so the re-walk can be taken
> exactly as written. What it is looking for is unchanged, and step 3 is still the one that finds
> bugs: a dialog and a dock built on one monitor and dragged to another at a different scale are
> where per-monitor DPI goes wrong, and neither existed when R-6 was first judged.
- **Goal** The case a single-monitor machine and an offscreen run both structurally cannot reach.
- **Pre** P-1 — two monitors, different scale factors, one changeable while the app runs.
- **Steps**
  1. Launch on the 100 % monitor. Drag the window to the 150 % monitor, and back. Twice.
  2. With the window on each monitor in turn, open the ribbon's every tab, the sidebar, a floating
     pane, and Preferences.
  3. **With the app running**, change the second monitor's scale in Windows Settings. Watch the window
     that is on it, and then drag the window across.
  4. Log out and back in at a different primary scale; relaunch.
- **Expect** No blurry text, no clipped label, no ribbon that keeps the old row height, no pane that
  keeps the old width in pixels rather than in points, no icon rendered at the wrong size. Step 3 is
  the one that finds bugs.
- **If it fails** **S1 against A3 if the ribbon itself is wrong**; S2 if it is a pane or a dialog —
  those are fixable without touching the ribbon verdict, and conflating them would wrongly sink A3.

**M-03 · Document tabs: tear off, re-dock, and back** ✅

> ✅ **Walked and signed off by the user, 2026-08-30**, in full: reorder, middle-click close,
> tear-off, re-dock at the start and at the end of the strip, `Ctrl+Tab` / `Ctrl+Shift+Tab`,
> `Ctrl+F6` / `Ctrl+Shift+F6`, the overflow strip, and closing the last tab in a torn-off window.
>
> **This walk found three defects, all in the product, and it is the first case in Stage 1 to do
> so** — every earlier issue in §8 was a test, a gate, a document or the CI. **I-33** (S3), tearing a
> window's only tab out left it empty; **I-34** (S2), re-dock could not be performed at all; and
> **I-35** (S3), a window emptied by a re-dock stayed open. All three were fixed and the case
> re-walked before this sign-off — the sign-off is on the repaired product, not on the repair.
>
> **The order they surfaced in is the lesson.** I-35 was unreachable until I-34 was fixed: nobody
> could re-dock, so nobody could empty a window by re-docking. A case interrupted by a defect is not
> finished when the defect is fixed; it is finished when it is walked again. And I-34 had been green
> in CI, described as shipped in four documents, and impossible to perform by hand for six days —
> because the case asserting it called `adoptDocument` directly and a comment recorded the gesture as
> manual-only. That is convention 19 (`CLAUDE.md`), and this walk is where it came from.
- **Goal** The structural behaviour D1-2 exists to let us test now rather than at Stage 4.
- **Pre** Three stub tabs open via `Ctrl+O` (any three `.pdf` files; nothing is parsed).
- **Steps** Reorder by dragging. Middle-click one closed. Tear one out to a new window. Drag it back
  and re-dock it, at the start and then at the end of the strip. `Ctrl+Tab` / `Ctrl+Shift+Tab` through
  them; `Ctrl+F6` / `Ctrl+Shift+F6` between windows. Open enough tabs to overflow the strip.
- **Expect** Every operation works and the tab's identity and title follow it. `Ctrl+F6` reaches the
  torn-off window. Nothing crashes when the last tab in a torn-off window closes.
- **If it fails** S2. Note whether the failure is in the *tab* or in the *window* — Stage 4 owns
  window arrangement, and a defect there is not this stage's to fix.

**M-04 · Panes dock, float, auto-hide, and remember** ✅

> ✅ **Walked and signed off by the user, 2026-08-30 — and it took THREE passes and four findings.**
> The first pass stopped at step 1: there was no View tab to click, because the ribbon's whole tab
> strip was laid out zero pixels wide (**I-36**, S2). The second, on the repaired build, went the
> distance and produced **I-37** (S3) — an auto-hidden sidebar that only a *click* on the edge strip
> would reveal, where *Expect* asks for a hover — and **I-38**, `general/touchMode` stored and read by
> nothing. Fixing I-37 created a gesture that had never existed before, so that step was walked a
> third time, and *that* walk found **I-39** (S3): the strip stayed on screen beside the pane it had
> just summoned. Fixed, walked once more, and confirmed — *"side panel looks normal now, all
> behaviors work as expected."*
>
> **The sequence is this case's real result.** Each repair made the next defect reachable: a tab strip
> nobody could click hid a pane gesture nobody could perform, which hid a strip nobody could watch
> disappear. Not one of the three was a regression — I-39 shipped at T9 and I-36 at T10, both green in
> CI the whole time, and I-37 was the product and the guide disagreeing about a gesture since T9. This
> is M-03's rule paying out a second time: a case is finished when it is walked again, not when the
> fix is written.
>
> **Two of the four were decisions rather than repairs, and the user took both.** I-37: hover or
> click are both defensible (Visual Studio hovers, VS Code clicks) — **hover**, so the product moved
> to the guide rather than the guide to the product, and the click still works because a strip that
> looks like a button and ignores a press would be a second defect. I-38: **planned absence, Stage
> 14** — now a row in §1 and in [`Stage1-AppShell-Plan.md` §11](./Stage1-AppShell-Plan.md#11-explicitly-out-of-scope),
> and *Steps* and *Expect* below no longer ask for it.
>
> **What the sign-off covers:** the log pane in all four dock areas, floated and re-docked; the
> sidebar auto-hidden, revealed by hover, and pinned; width memory across hide/show, auto-hide/pin and
> float/re-dock; and `general/uiFontScale` at 1.5 applied live with no clipped label. Width across a
> **restart** is Stage 4's `SessionState` and was expected to forget.
>
> ⤵ *The note below records why this case stood amber from T14 until the walk. Kept as a breadcrumb.*
>
> 🟡 **Partly walkable as of T14.** The **Log pane** is real (T14): `pane.log` from the View tab, then
> dock, float, move and re-dock it in any of the four areas. **Auto-hide and width memory are
> `SidebarDock`'s**, not the log pane's — walked against the **sidebar**, which has had both since T9.
> Neither pane survives a restart yet, so the close-and-reopen step is expected to forget; reporting
> that as a defect would be filing a planned absence.

- **Steps** Dock the Log pane left, then right. Float it; move it to the second monitor; re-dock it.
  Auto-hide **the sidebar** and bring it back from the edge strip. Resize it, close the app, reopen.
  Then set `general/uiFontScale` to 1.5 **in Preferences** and repeat the resize. *(`general/touchMode`
  was in this list until 2026-08-30; it is a planned absence — §1, I-38.)*
- **Expect** Width is remembered; auto-hide reveals on hover and hides on focus loss; the font scale
  does not clip labels. *(“Touch mode gives visibly larger targets” was struck on 2026-08-30: the key
  is a planned absence until Stage 14 — §1, I-38.)*
- **If it fails** S3, unless a pane cannot be re-docked at all — S2.

**M-05 · Theme follows the system** ✅

> ✅ **Walked and signed off by the user, 2026-08-31 — two passes, two findings, and the second one
> only existed because the first fix changed nothing on screen.**
>
> **I-40** (S3), 2026-08-30: dark mode's **disabled** text could not be read — and disabled text is
> most of this shell, because 360 commands are declared and 22 registered. Measured 3.05 against the
> window and 2.62 against a pane. The light theme, already signed off twice, measured **2.54** and was
> the worse of the two: M-01 and M-02 walked light only, since nothing before this case had a reason
> to switch the OS. Both raised, and `tst_theme` now measures every foreground token against the
> background it is actually painted over, in both themes, as a ratio against a floor.
>
> **I-41** (S2), 2026-08-31: with the palette corrected the text was *still* unreadable, and that is
> what pointed away from the palette. Qt's native `windows11` style paints control text from the
> colour scheme **it** derives from Windows and ignores the application palette — so the theme reached
> every `QLabel` and no `QToolButton`, which is the ribbon, the toolbar and the tool finder. Measured
> by rendering a widget and reading the pixels back: an enabled button's glyph was `#1f1f1f` on a
> `#1f1f1f` background. `apply()` now sets `QStyleHints::setColorScheme()` as well, `Unknown` for
> follow-system so the OS keeps the decision.
>
> **What this case is worth remembering for.** A correct fix to a real defect changed nothing a person
> could see, and saying so out loud is what found the defect underneath it. Measuring the colour table
> confirmed the colour table; only rendering the widget said what reached the screen. Both halves of
> the walk — follow-system tracking an OS flip live, and an explicit Dark holding against a light-mode
> OS — were re-walked on the repaired build before this sign-off.
>
> ⤵ *Two breadcrumbs kept: why the case was thought unwalkable, and one colour it cannot reach.*
>
> **Mis-listed as not walkable until 2026-08-30.** Both halves had existed since T14 — `ThemeManager`
> (T9) connects `QStyleHints::colorSchemeChanged` and re-applies only when the request is
> `FollowSystem`, and `general/theme` has been an editable row on the General page since T14. The
> stale line covered M-05 … M-07, M-09 and M-10 as a block; the other four are still correct.
>
> **One colour in the shell is not a theme token and cannot be walked here:** the contextual tab's
> text is a hard-coded purple (`ui/shell/Ribbon.cpp`), and nothing outside a test can show a
> contextual tab in Stage 1. A real thing to fix when contextual tabs get content (Stages 5–13), not
> a finding this case can produce.

- **Steps** With `general/theme=FollowSystem`, switch Windows between light and dark **while RawPage is
  running**. Then set RawPage to Dark explicitly and switch Windows again.
- **Expect** Follow-system tracks live, with no restart and no half-themed widget. An explicit choice
  wins over the system.
- **If it fails** S3 for a mis-tinted widget; **S2 if follow-system needs a restart** — a theme that
  only applies at launch is not a follow-system theme.
- ⤵ **Narrowed 2026-08-23.** The third step turned on a Windows high-contrast theme and expected our
  theming to yield to the OS palette (decision **D1-15**). D1-15 was withdrawn with the accessibility
  removal, so `ThemeManager` no longer consults the OS high-contrast setting and there is nothing left
  to walk. See [`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23).

**M-06 · The `zh-TW` UI is right, not merely present** ✅

> ✅ **Walked and signed off by the user, 2026-08-31.** Every surface: all 9 ribbon tabs with their 49
> group captions and 192 control labels, the disabled-command tooltips, the QAT and both of its
> context menus, the tool finder searching in Chinese, the log pane, all three Preferences pages, the
> placeholders including the `%1` format string, the window title and the status bar — **no truncation,
> no English fallback, no collided mnemonic** — and the editorial review of the 29 unfinished entries.
>
> **Four defects, and the walk found every one of them by using the product rather than reading it.**
> **I-42** (S2): `Alt` raised no key tips **in any locale** — drawn since T10, never triggered, and
> R-5 had been signed off against the deleted T2 spike; the tip letters were also the first character
> of each tab title, which in Chinese gave 常 建 編 註 組 表 保 檢, none typable, with 表 claimed twice.
> **I-44** (S2, and the worst this stage has produced): a query matching nothing **froze the
> application**, because the result list is a `Qt::Popup` holding the mouse grab and nothing could
> dismiss it. **I-45** (S2): the fix for I-44 closed the list in the same turn it opened — showing a
> popup is itself a focus change — so for one build no search returned anything, caught by re-walking
> the same step within the hour. **I-43** (S3) surfaced here and belongs to **M-11**: reordering the
> QAT was reachable only from a test while three documents called it shipped.
>
> **Four corrections to this case's own text, made before or during the walk rather than found
> inside it.** There is no language chooser in the UI and that is a **planned absence** — the key is
> read, the page is Stage 14, and the two routes are `--lang` and `settings.ini`, both taking effect
> at the next launch. Chords are deliberately **not** localized, so `Ctrl+Shift+K` on the Shortcuts
> page is a decision to judge rather than an untranslated string. The steps said *"both Preferences
> pages"* when three ship. And the unfinished count was **29**, not the 28 this guide had carried
> since T9 — 15 of them on the ribbon, 14 reachable only through the tool finder.
>
> **One thing the walk decided rather than fixed**: the log pane's **records** stay English in every
> locale. Its chrome is fully translated; the records are diagnostics that get pasted into bug
> reports, and `Copy` copies exactly what is displayed. Undocumented until this walk filed it, and now
> a §7 caveat row.

- **Goal** P-3. `i18n-lint` can prove every key exists. Only you can see that a label is wrong, reads
  like machine output, or does not fit.
- **Steps** Launch `rawpage.exe --lang zh-TW`. Walk every ribbon tab, the QAT **and its context
  menu**, the tool finder, the status bar, the sidebar, the log pane, and **all three** Preferences
  pages. *(This said “both Preferences pages” until 2026-08-31: Logging shipped alongside the log
  pane at T14 and the case was never updated.)* Look specifically for: truncation and
  ellipsis; a mnemonic that now collides or has no letter to attach to; a chord label rendered in the
  wrong order; font fallback that makes some strings look different from others; and any string still
  in English.
- **Expect** No truncation, no English fallback, no collided mnemonic, and terminology that a PDF user
  in Taiwan would recognize.
- **If it fails** S3 for wording, **S2 for truncation or an English fallback** — the first is a
  translation fix, the second is a layout or catalog bug. Please mark which; they go to different
  people.

**M-07 · Explorer double-click into a running instance — flow B end to end** ☐
- **Goal** The one behaviour `ISingleInstanceBus` exists for (D1-11), plus the D1-3 evidence.
- **Pre** File association registered by `WinShellIntegration`. The two awkward filenames from §2.
- **Steps**
  1. RawPage not running: double-click a `.pdf` in Explorer.
  2. RawPage running and **behind another window**: double-click a second `.pdf`.
  3. Repeat step 2 with the **CJK** filename, then with the **emoji** filename.
  4. RawPage minimized: double-click a fourth.
  5. Select three files and press Enter.
- **Expect** One process throughout. The window **comes to the foreground** each time — not a flashing
  taskbar button. The filename is correct in the tab for both awkward names. Three files give three
  tabs.
- **If it fails** **S2.** Distinguish carefully: *forwards but does not raise* is
  `AllowSetForegroundWindow` (D1-11); *wrong filename* is the `-municode` verdict (D1-3) and A-05 was
  wrong; *two processes* is the mutex. Three different bugs with one symptom, so please say which.

**~~M-08 · Keyboard-only, and a reduced screen-reader pass (R-8)~~** ⤵ withdrawn

⤵ **Withdrawn 2026-08-23** with criterion **R-8** and precondition **P-2**, when accessibility was
removed from the product — see
[`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23).
The heading is kept as a breadcrumb so M-09…M-12 keep their numbers and existing cross-references
still resolve.

**What did NOT go with it.** *Keyboard-only operability is retained product behaviour.* Every command
must still be reachable without a mouse, focus must still be visible, and `Tab` must still visit
controls in visual order — that last one is asserted mechanically by the ribbon spike's focus-order
self-test (issues **I-05**, **I-06**), so it needs no manual case here. Stage 14 carries the
whole-application keyboard pass. What is gone is the screen reader, the roles, and the names.

**M-09 · The tool finder and a remapped chord** ☐
- **Steps** `Ctrl+Shift+U`; type a partial command name; run it from the list. Then open Preferences →
  Shortcuts, remap a chord to one that shadows a Global chord in one context, accept the reported
  shadow, close, and use both the new chord and the shadowed one **in each context**. Restart and
  confirm the remap survived. Reset to defaults.
- **Expect** The finder shows each match's current chord, including the remapped one. The shadow is
  **reported and allowed** — §8 ships six of these by design. Reset restores all defaults.
- **If it fails** S2 if a remap does not take effect or does not persist; S3 for finder ranking.

**M-10 · A real crash produces a symbolizable dump** ☐
- **Goal** A-65 does this in a controlled way. This does it the way it will actually happen.
- **Steps** With a debug build, trigger `--crash-test` from the UI. Then find the dump yourself using
  only what the app told you, and run `scripts\symbolize.ps1`.
- **Expect** You can locate the dump from the message alone, and the symbolized output names a
  function in our code.
- **If it fails** S3 if the message is unclear; **S2 if you cannot find or symbolize it** — a dump path
  only I know is not a crash-reporting path.

**M-11 · Everything survives a restart** ☐ — ✅ **fully walkable as of T14**
- **What T14 changed:** every step now has a surface. The theme and the font scale are set on the
  **General** page instead of by hand in `settings.ini`, and the chord remap is made on the
  **Shortcuts** page instead of by editing `shortcuts/<context>/<action>` with a text editor. Walk
  it as written; nothing is left unticked.
- **One thing to look for that a test cannot.** The Preferences dialog applies immediately and has
  **no OK or Cancel** — that is deliberate (a `Cancel` that must un-apply needs a transaction
  `core::Settings` does not have), but it is exactly the sort of decision that is right in a header
  and wrong in front of a person. If closing the window felt like it should have discarded
  something, say so: that is a finding this stage would rather have now than at Stage 14.
- **Steps** Customize the QAT; move it below the ribbon; resize and auto-hide a pane; set a theme; set
  a font scale; remap a chord; open three tabs; pin one recent file. Quit properly. Relaunch. Then
  quit by **killing the process** and relaunch again.
- **Expect** After a clean quit, every customization is back and the window geometry is restored.
  After a kill, **the same** — settings are written when changed, not at exit. Documents are **not**
  restored: `SessionState` is Stage 4, and their absence is correct here.
- **If it fails** S2 if a customization is lost after a **clean** quit; S3 after a kill.

**M-12 · The stage's honest emptiness is legible** ☐ — 🟡 **fully walkable as of T13**
- **What T13 changed:** the keyboard works, so the steps below can be walked as written. `Ctrl+Z`,
  `Ctrl+S` and `Ctrl+P` still do nothing — and that is now the *correct* nothing rather than an
  absent mechanism: those ids are declared and unregistered, so the chord resolves, the dispatch is
  refused as `NotFound`, and the key is deliberately **left unconsumed** for whatever else might want
  it. `Ctrl+O`, `F4`, `F9`, `F11` and `Ctrl+W` do work, which is the contrast the walk should notice.
- **And this is the walk that can see what the harness cannot:** a chord the window manager, the IME
  or the platform intercepts before Qt ever sees it. No synthetic key event can reproduce that.
- **Goal** Stage 0's M-01 found that the most expensive defects were things the documentation did not
  say. The equivalent here is a UI that lies about what it can do.
- **Steps** Use the app for ten minutes as if you did not know it was Stage 1. Click the disabled
  ribbon buttons. Open a stub tab. Press `Ctrl+Z`, `Ctrl+S`, `Ctrl+P`.
- **Expect** A disabled button explains *why* — "arrives in Stage 7" reads differently from "not
  available for this document", and plan §5.1 requires those to differ. The stub tab does not pretend
  to be a document. Nothing claims to have saved, printed, or undone anything.
- **If it fails** S3 — but **an action that silently appears to succeed is S1**, because that is the
  exact failure mode the batch runner will inherit at Stage 14.

---

## 7. Known caveats — verify, do not assume broken

Things that will look wrong and are not. Each needs a **human verdict** during the manual pass, not an
assumption — Stage 0's sign-off criteria required exactly this and it was worth it.

| Looks like a bug | Is actually |
|---|---|
| A tab shows a filename and a grey canvas with no page | D1-2, working as designed. Stage 2 fills it in |
| Many ribbon buttons are visibly disabled | **360 ids declared, 22 registered** (358 and nineteen until 2026-08-31: I-42 registered `app.ribbonKeyTips`, and I-43 **added** `ui.qatMoveLeft` / `ui.qatMoveRight`, the only two ids this stage has added to the plan's 358) — the figures the plan forecast as *"~200 declared, ~60–80 registered"* were both wrong, and the honest state is further apart than that. The tooltip should say which kind of disabled — if it does not, **that** is the bug (M-12) |
| `Ctrl+Z` does nothing, and neither does anything else it is typed into | **Corrected 2026-08-25 at T15 — the caveat this row used to carry was wrong.** It said *"registered, dispatching, empty journal until Stage 3"*. `Ctrl+Z` IS bound: `KeyBindingDefaults.def` maps it to `EditUndo` in `Common`, and `ShortcutBinder` resolves it. But `EditUndo` is **not registered** — it is not one of the nineteen — so `dispatch` returns **`NotFound`**, and by T13's deliberate design a `NotFound` **does not consume the key**. There is no journal to be empty; `CommandJournal` is Stage 3. So the correct expectation is *"nothing happens, and the keystroke passes through to whatever had focus"* — which is why a `QLineEdit` still gets its own `Ctrl+Z`. The same is true of every one of the ~339 unregistered ids |
| `checks` runs three gates, not four | `mutation-lint` needs `Command`, Stage 3. If it ever *claims* four, that is the bug |
| `portable-asan` does not run any UI test | D1-4, deliberate. The portable subset is `core/` + `pdf/cos/` by rule |
| No golden-image or round-trip suite ran | They have no subject until Stages 2 and 3 |
| `zh-CN` and `ja` are missing | D1-14, declared `"shipped": false`. A-15 asserts the gate *says so* |
| A second user on the machine gets their own instance | D1-11's `Local\` scope, deliberate |
| The log pane's **records** are English in a `zh-TW` UI | Deliberate, and confirmed by the user during M-06 on 2026-08-31. The pane's own chrome IS translated — title, Level, Subsystem, All subsystems, Copy, Clear, the threshold sentence, and every level and subsystem name. The RECORDS are diagnostics: the `[level][subsystem]` tokens come from `core::logLevelName` as ASCII and the messages are English literals at the log sites, so a log pasted into a bug report reads the same everywhere and **Copy copies exactly what is displayed**. It was undocumented until a walk filed it, which is why this row exists |

---

## 8. Issue log

Everything found in §5 or §6 goes here. This table is the fix-pass backlog and the stage's progress
record.

> **One numbering collision, named rather than left to trip a reader.** This log now has its own
> **I-15** and **I-16**, and this document also cites Stage 0's **I-15** (*a rule tested against one
> spelling catches one spelling*) and **I-20** (*a waiver census that counts its own documentation*)
> in prose — in I-03's row and in §5.2. Those two references are **Stage 0's**, from
> `Stage0-Toolchain-Testing-Guide.md`. Every `I-nn` in the *State* and *Repro* columns of this table
> is Stage 1's. `CLAUDE.md` requires a stage-scoped label to carry its scope; this is that.

| # | Case / area | What happens | Expected | Sev | Repro / notes | State |
|---|---|---|---|---|---|---|
| I-01 | A-31 / A-32 · `Features.md` §8 | The **"twelve deliberate collisions"** figure could not be reconciled with §8. A tree-wide census gives **6** chords where any context redefines a `Global` chord, **9** where a context overrides a `Common` one, **15** for the two together, and **52** chords appearing in more than one table — none of them twelve. | The quoted figure matches a census of the tables it refers to, under a stated definition | **S3** | Found 2026-08-19 while writing this guide, **before any Stage 1 code**, by parsing §8 rather than reading it. Pre-existing — the figure predates this stage | **Closed 2026-08-20. The figure was right; it was never *scoped*.** Twelve counts **Write mode's** collisions specifically: **8** overriding Viewer (`Ctrl+B` `Ctrl+E` `Ctrl+L` `Ctrl+R` `Ctrl+U` `Ctrl+=` `Ctrl+Shift+=` `Ctrl+Shift+N`) and **4** overriding Global (`Ctrl+D` `Ctrl+K` `Ctrl+Shift+A` `Ctrl+Shift+L`), 0 overriding Common. The clue was Stage 13's own wording, *"the twelve deliberate collisions with the Viewer context"* — right that Write mode is the scope, wrong that Viewer is the only counterpart. Scope now stated in [`Features.md` §8](./Features.md#what-the-twelve-deliberate-collisions-counts); Stage 13's sentence and `CLAUDE.md`'s corrected; A-31 asserts the 12, A-32 the 6 and 9 separately |
| I-02 | A-07 · build id | The case said `objdump -s -j .note.gnu.build-id rawpage.exe`. That section **does not exist** and never will: `.note.gnu.build-id` is the ELF spelling, and this is PE. GNU ld honours `-Wl,--build-id` by emitting a CodeView **RSDS** record — a 16-byte GUID plus a 4-byte age — into a **`.buildid`** section instead. Run as written, the case fails on a build that is perfectly correct. | The case names a command that reads the build id this toolchain actually emits | **S3** | Found 2026-08-23 running T1. Confirmed both ways on the pinned toolchain: with the flag, `.buildid` (0x35 bytes) appears and `.note.gnu.build-id` does not; without it, neither does. Same shape as Stage 0's **I-19** — a case written from the Linux spelling of a Windows fact | **Closed 2026-08-23.** A-07 rewritten to `objdump -s -j .buildid`. The correction **strengthens** the case: because `MiniDumpWriteDump` copies that same RSDS record into a dump's module list, the id in the binary, the id `--version` prints, and the id in a dump can all be compared — which is exactly how `scripts/symbolize.ps1` refuses a mismatched binary |
| I-03 | A-05 · the D1-3 probe | The first `--print-args` implementation reported `municode=yes` on a build with `-municode` nowhere in it. `Qt6::Core` puts `UNICODE`/`_UNICODE` in its INTERFACE compile definitions, so the probe was measuring **Qt's** preprocessor state, not the linker flag — and would have reported a **pass for the wrong reason** on the one case whose entire job is to settle a deferred flag question. | A diagnostic that reports a build flag reports **that flag** | **S2** | Found 2026-08-23 by reading the output rather than the exit code. I-15's lesson in a new costume: a check that cannot fail for the reason it claims to test | **Closed 2026-08-23.** `--print-args` now prints `municode=` from `RAWPAGE_MUNICODE`, defined in `cmake/RawPageWarnings.cmake` **beside the link option it describes**, so flag and diagnostic cannot drift; `_UNICODE` is still printed, labelled as Qt's and explicitly unrelated. It also now prints `activeCodePage=`, without which the `argv` result is unreproducible on another machine |
| I-04 | A-05 · scope of the verdict | Not a defect — a **consequence** the case does not state. `argv` is narrow, so the CRT transcodes the wide command line through the ACP and replaces anything unrepresentable with `?`. On this machine (ACP **950**, Big5) the emoji path came back as `report-?????.pdf` in `argv` while `QCoreApplication::arguments()` was byte-perfect. Any future `argv`-based path read would be silently and unrecoverably wrong. | The verdict carries the rule it implies, not only the flag decision | **S3** | Found 2026-08-23 as part of the T1 measurement. Note that a machine with a UTF-8 ACP would have shown *nothing* here — the finding exists because this machine's ACP is 950 | **Closed 2026-08-23 at T8, by three mechanisms rather than one** — because no single one of them covers the whole rule. (1) **There is no `argc/argv` overload to call.** `CommandLine::parse` takes a `QStringList`, and `tests/unit/compile/argv-overload.cpp` proves a caller who tries does not compile, with `arguments-list.cpp` as the control so the case is not satisfied by a header nobody can include. (2) **`tools/layer-lint` gained a non-escapable `argv-path-read` token rule**, allowed only in `app/main.cpp` — the one file that legitimately holds `argv`, since it hands it to `QApplication` and prints it for the `--print-args` measurement that produced this rule in the first place. Fixture `gates.layer_lint.argv_path_read` plus the entry-point control. (3) **`tst_commandline` asserts an emoji path survives the parser byte for byte.** No runtime test can prove the *whole* rule — a mangled path is a perfectly valid `QString` — which is exactly why (1) and (2) exist. **Proven end to end the same day:** a second launch forwarded `測試-📄.pdf` across the command line, `QCoreApplication::arguments()`, a named pipe and a queued invocation, and the primary logged it unchanged |
| I-07 | A-20 · test naming | `ctest` reported *"unit.every KeyContext value has a name and the count matches Features.md §8"* as **failed** while the test body passed. `catch_discover_tests` registers the Catch2 name with CTest; CTest passes it back as a command-line filter; the `§` does not survive that round trip (it arrived as `Features.md ??8`), so Catch2 matched no test and CTest called that a failure. | A test either passes or fails on its assertions, never on how its name is spelled | **S3** | Found 2026-08-23 running T3. **I-04's root cause in a second place** — a narrow, code-page-bound process boundary — and this machine's ACP of 950 is again what exposed it. A UTF-8 ACP would have hidden it until CI ran somewhere else | **Closed 2026-08-23.** The case was renamed to ASCII and the reason recorded in a comment above it, so the next person does not rediscover it. **Standing convention for the whole project: Catch2 test names are ASCII; section references go in comments.** Recorded in the Stage 1 status block |
| I-05 | M-01 · ribbon spike, R-8 | Pressing `Tab` around the Home tab visited **Clipboard → View → Tools**, skipping straight from *Format/Clear* to *Single page/Rulers* and reaching *Select*/*Hand* last. The groups read left-to-right **Clipboard, Tools, Styles, View**. | `Tab` visits controls in the order they appear on screen | **S2** | **Found by the user walking M-01, 2026-08-23** — no automated case existed that could have caught it. Root cause measured with a new `--print-tab-order` dump, not guessed: focus order was **exactly the groups' collapse priority** (Styles 4, Clipboard 3, View 2, Tools 1). Every group collapses during the first layout, before the window has its real width, and re-expands in priority order; `RibbonGroup::setCollapsed` implements collapse by **re-parenting**, and re-parenting appends the moved subtree to the **end** of the window's focus chain. So Tab order was a fossil of the order the groups happened to change in | **Closed 2026-08-23.** `RibbonPageLayout::restoreTabOrder()` re-chains `setTabOrder` across the groups in visual order whenever the collapsed set changes. **Verified by negative control**: with the call commented out the new case fails with *"Tab reached 'Paste' in group 'Clipboard' (visual position 0) after already leaving group 2"*. The dump also exposed a second defect — the Rulers checkbox had no `accessibleName` and announced as `QCheckBox` — now fixed and asserted. **This is not a spike artefact:** any collapse built on re-parenting has it, silently, and only under the collapse sequence that actually occurred. T10 inherits both the fix and the case |
| I-06 | §6 M-08 · scope | R-8 was filed as **wholly manual**. Half of it — that `Tab` visits controls in visual order and that every control carries a name — turned out to be measurable, and I-05 is the proof, since it is exactly the half a person found by hand. | A manual case that could have been automated is a defect in this guide | **S3** | Raised 2026-08-23 off the back of I-05, per `CLAUDE.md`: *"the manual part is the residue, and it should shrink as the harness grows"* | **Closed 2026-08-23.** The spike's `--self-test` gained an **R-8 focus-order case**, and M-08 is narrowed to what genuinely needs Narrator: whether each control is **announced with a sensible role and a label a person would recognise**. The order half is now machine-asserted on every run |
| I-08 | A-43 / A-44 · `settings.ini` | A hand-written `[general]` section was **silently ignored**. `general/uiFontScale=9.0` — the exact edit A-43 instructs a human to make — read back as the default, because `QSettings` treats an INI section named `General` (case-insensitively) as its own reserved fallback section: its keys come back with **no group at all**, and QSettings writes our `general/` group out as `[%General]` to dodge the same collision. | The edit the guide asks for takes effect | **S2** | Found 2026-08-23 at T5 — by running the case, not by reading the code. Six tests failed on it at once, all of them fixtures written *as a person would write the file*. It would never have shown up against a file the product itself wrote, since that round trip is self-consistent | **Closed 2026-08-23.** `Settings::loadFromFile` folds a top-level key into `general/` **only when that yields a declared key**, so a genuinely top-level key a newer build wrote is still preserved as itself; the value is then queued to be rewritten under the canonical path **and** the old spelling queued for removal, so the file converges on one spelling. The first fix did only the removal half and deleted the user's hand edit — caught by the case that asserts a version-current file is left byte-identical. A dedicated case now asserts the whole behaviour |
| I-09 | A-45 · `logging/subsystems` | `subsystems=render,teleport,ocr` read back as **the default (all seven)**. `QSettings`' INI reader turns any comma-containing value into a `QStringList`, and `QVariant::toString()` on a QStringList returns an **empty string** — so the codec saw "no value" and fell back. The symptom is a setting that ignores what the file plainly says, with nothing logged and nothing failing. | A value in the file is the value that takes effect, or a log line says why not | **S2** | Found 2026-08-23 at T5 by the same run as I-08. The one-element case is the same family from the other end: a list written with a single entry comes back as a bare `QString`, so `toStringList()` yields an **empty** list — i.e. pinning exactly one file would lose it on restart, which is the first thing anyone tries | **Closed 2026-08-23.** Every scalar codec reads through `settingsVariantText()`, which joins a QStringList back with commas; the `QStringList` codec checks the variant's type and wraps a bare string. Both have a case, and both cases fail if the handling is removed. **Standing rule, now in `CLAUDE.md`:** `settings.ini` is read through our own codecs, never through `QVariant`'s conversions |
| I-10 | A-62 / plan §6 T7 · the platform interface count | The plan says **fifteen** interfaces. `Tech-Stack-Plan.md` §4.3's table names **sixteen** in fifteen rows — `ICertificateStore` and `ICryptoProvider` share one — and `platform/win/WinPrinting` has had a row in `Architecture-FileMap.md` §3.12 since the map was written with **no interface anywhere for it to implement**, which would have made that file a layering violation on the day it was created. | The declared surface covers every `platform/win/` file the map plans, and the stated count is the number of interfaces | **S3** | Found 2026-08-23 at T7 by listing the two documents against each other before writing the header, rather than by transcribing one of them. The "sixteen to fifteen" the accessibility removal recorded was a count of ROWS; nobody had counted interfaces | **Closed 2026-08-23.** `IPrinting` declared and given its own row in §4.3, which owns the figure; the count is now stated as **seventeen interfaces in sixteen rows**, with the row/interface distinction written down so it cannot be miscounted the same way again. `Features.md`, `CLAUDE.md`, `Implementation-Plan.md` and the Stage 1 plan all corrected. `tests/platform` asserts `kPlatformInterfaceCount` against the header's own name table, so the number and the surface cannot drift |
| I-11 | plan §6 T7 · `WinSingleInstanceBus` | `forward()` failed with `ERROR_FILE_NOT_FOUND` whenever it ran shortly after `listen()`. `listen()` started a worker thread and returned; the thread created the pipe. Between those two moments the pipe **did not exist**, so a secondary launched in that window got a hard failure — a launch that silently does nothing, which is exactly the symptom D1-11 exists to prevent. | A handoff attempted at any time after `listen()` returns is delivered | **S2** | Found 2026-08-23 at T7 **by the tests, on their first run** — three of the nineteen failed. The window is small and entirely real: it is a user double-clicking a second PDF while the first instance is still starting, and it would have reproduced on a slow machine and nowhere else | **Closed 2026-08-23.** Two fixes, because the race has two halves. `listen()` now creates the first pipe instance **synchronously** before returning, so the state "claimed but not listening" no longer exists; and the server loop puts the **next** instance up immediately after accepting a connection rather than at the top of the next iteration, so a second forward arriving while the first is being read is not refused either — that is the multi-file "open with" case, not an exotic one. `forward()` additionally retries `ERROR_FILE_NOT_FOUND` and `ERROR_PIPE_BUSY` for up to three seconds and nothing else, since retrying an access-denied only delays the same failure. The three failing cases are the negative control, observed failing before the fix and passing after |
| I-12 | §5.6 · `ui/shell/MainWindow` | **Segfault on window destruction.** `~QWidget` closes the window, which hides its children, which makes `QDockWidget` emit `visibilityChanged`, which invokes a lambda connected with `this` as the receiver — on an object whose `MainWindow` part has already been destroyed. Qt drops such a connection when the receiver's **QObject** part goes, and that happens last. Offscreen it faults inside `qt_metacall`; on a real desktop it would be whatever the freed memory happened to hold. | A window can be destroyed | **S1** | **Found by `tests/ui` on its first run, 2026-08-23 at T9** — the same way T7 found I-11, by running the thing rather than reading it. Note it is invisible to any test that never destroys a window, which is every test that leaks one | **Closed 2026-08-23.** `~MainWindow` disconnects the sidebar explicitly before the base classes tear down. **Verified by negative control:** with the `disconnect` line commented out, `ui.auto-hide and pin round trip and peeking is not a state change` segfaults and no other case does; with it, 226/226. The pattern generalises and T10–T14 inherit it — every widget this shell grows will connect a child's signal to a lambda holding `this` |
| I-13 | §2 · settings path | Preferences were written to **`%APPDATA%\RawPage\RawPage\settings.ini`**, one level deeper than the `%APPDATA%\RawPage\settings.ini` that `Implementation-Plan.md`'s Stage 1 section specifies. `QStandardPaths::AppDataLocation` is `%APPDATA%\<org>\<app>`, and `app/main.cpp` was setting both the organization and the application name to `RawPage`. | The settings file is where the owning document says it is | **S3** | Found 2026-08-23 at T8, on the **first end-to-end run of the composition root**. T5 could not have caught it: every one of its tests passes an explicit path, deliberately, so that running the suite does not rewrite the settings of the person running it — so T8 is the first caller that ever reaches `defaultFilePath()` | **Closed 2026-08-23.** The organization name is no longer set, and the reason is written down where it is not set: **we are the organization**, nothing in the project reads the value, and `AppDataLocation` then resolves to exactly `%APPDATA%\RawPage`. `GenericConfigLocation` was tried first and rejected on measurement — on Windows it is `%LOCALAPPDATA%`, and a preference that does not roam is one that is missing on the user's second machine |
| I-14 | §5.5 · settings persistence | **Nothing ever called `Settings::sync()`.** Every preference and every recent-files entry was correct for the life of the process and gone at the end of it. `Settings` accumulates dirty keys and commits on `sync()` — which T5 built, tested, and left without a caller, because T5 has no process to end. | A setting set in one session is there in the next | **S2** | Found 2026-08-23 at T8 by running flow B and then looking at the file: the forwarded path appeared in the primary's log and not in `settings.ini`. Invisible to `tst_settings`, which calls `sync()` itself because that is the function it is testing | **Closed 2026-08-23.** `CompositionRoot`'s destructor syncs and reports a failure by name. Placed there rather than on `Application::aboutToQuit` because a secondary instance exits before any event loop runs and a diagnostic run exits before that; a process that changed nothing writes nothing, so the wider placement costs nothing. **Verified against the real executable:** a hand-written `[general] theme=dark` with no `meta/schemaVersion` was migrated, restamped and preserved across a clean exit |
| I-15 | §5.2 · `layer-lint` rules | **`ui/` had no permitted path to `platform/platform.h`.** That header's own comment has said since T7 that it is *"included by `core/`, `ui/` and `app/`"*, and `core` and `core-settings` both carry it in `mayInclude` — `ui` never got the entry, because until T9 no `ui/` file existed to need it. The first shell widget that asks the OS bundle what it cannot do would have failed the gate. | The rules file matches the architecture the documents describe | **S3** | Found 2026-08-23 at T9 by writing the first `ui/` file that needs the OS boundary. Same shape as **I-10**: a gap between two documents that only a line of code can expose. Note this is a **widening**, which is a licensing-grade decision — recorded here rather than made quietly in a diff | **Closed 2026-08-23.** `ui`'s `mayInclude` gains `platform/platform.h` and `app/Services.h`, and **each is one file, not a layer**. `app/Services.h` additionally gets its own layer entry constraining it to `core/` plus `platform/platform.h`, so a `pdf/` include added there at Stage 2 cannot reach every widget through the one open door. Four gate cases: the violation and the control for each direction |
| I-16 | §5.2 · gate file coverage | **Neither gate scanned `.def` files.** This project keeps its permanent public names in X-macro tables — `ActionId.def`, `KeyBindingDefaults.def`, `SettingsKeys.def`, and now `ShellKeys.def` — which are `#include`d straight into translation units. `i18n-lint` therefore reported all 22 new `shell.*` keys as orphans on a tree that references every one of them; and `layer-lint` could not have seen an `#include` written inside a table that lands in a layer with a deny list. | A gate reads the files the compiler reads | **S3** | Found 2026-08-23 at T9 by the orphan report, which was right that it could not find the references and wrong about what that meant. The `layer-lint` half is the more interesting one: it had been open since T3 and nothing had exercised it | **Closed 2026-08-23.** `.def` added to both gates' `extensions`, with the reasoning in each rules file. Whether a file is COMPILED is a fact about the build; whether it is part of a layer, or holds a user-facing key, is what the gates are for. `layer-lint` now scans 100 files where it scanned 96, still clean; `i18n-lint` 45 files, 0 orphans, 0 waivers |
| I-17 | §5.5 · A-46 · `tst_recentfiles` | **`unit.the same document reached two ways is one entry` fails on Linux, and only on Linux** — `2 == 1`. `RecentFiles::canonical()` deduped through `QDir::cleanPath`, which converts **platform-native** separators: on Windows a backslash is one, under POSIX it is an ordinary filename character, so `C:\docs\a.pdf` stayed a second entry. Written at **T5**, green on Windows ever since, and never once run on Linux until the CI push after T9. | One dedup rule, the same on both builds | **S2** | Found 2026-08-23 by the **first `portable-asan` run to reach Stage 1's code** (run 32649389697, 176/177). The interesting half is the eleven-task gap: the `portable-asan` job had not run since before T3, so every `core/` test T3–T9 added was Windows-verified only. Not reproducible locally in either direction — on Windows the fix is unobservable, which is the whole shape of the bug | **Closed 2026-08-23.** `canonical()` normalizes separators **by hand** and then calls `cleanPath`, so the rule no longer depends on the host. Fixing the *test* instead — `#ifdef Q_OS_WIN` — was the smaller diff and the wrong one: it would leave the sanitized build exercising a different dedup rule from the shipping build, and a finding there has to be reproducible in `x64-debug` before it counts as fixed. The case also now asserts **which** spelling survives, since a count of 1 is satisfied by collapsing onto the wrong one |
| I-18 | §5.6 · A-54 · `tst_actionwiring` | **The wiring self-check called the sidebar a bare `QAction`.** Its first run against a real, shown `MainWindow` reported `bare QAction "Panes"` — which is `QDockWidget::toggleViewAction()`, an action Qt creates the moment a dock exists. The ribbon's overflow chevron has the same shape through `QMenu::menuAction()`. Neither was written by anyone and neither can be prevented. | The check reports actions somebody wired, not actions Qt manufactures | **S3** | Found 2026-08-24 at T10 the only way it could be: by running the check against the product rather than against a fixture. Two tasks running, two self-checks taught something by their first real subject — the other was **I-12** | **Closed 2026-08-24.** `wiringComplaints()` skips an action that is its own parent widget's intrinsic action, recognised **by identity** (`menu->menuAction() == action`) rather than by type or name, so an ordinary action that merely sits on a menu is not excused. The residual gap is stated in the code: passing one of these to a command surface would go unreported, because its parent does not change when it is used |
| I-19 | §5.6 · A-50 · the shipped ribbon | **The ribbon held the entire main window open at 1044 logical pixels, and hid a second defect behind it.** `RibbonPageLayout::minimumSize()` summed every group's collapsed width, and that minimum propagates to the window — on a 1366-wide laptop at 150 % scaling the window could not be made to fit the screen, with nothing on screen to explain why. Behind it: because the widget snapped back to 1044 on any narrower request, **the tab strip could never run out of room**, so the chevron overflow — one of the three pieces of named custom work the A3 verdict rests on — was unreachable in the shipped configuration while its code looked correct. `QTabBar` with `setUsesScrollButtons(false)` was independently demanding the full width of all nine tabs. | A ribbon narrows, then collapses, then clips — it does not veto the window size | **S2** | Found 2026-08-24 at T10 by a case that asked the ribbon to be 240 px wide and then **checked what width it actually got**. The spike never saw it: four groups, not forty-nine. An assertion that had trusted `resize()` would have reported overflow-not-triggered as a plain failure and taught nothing | **Closed 2026-08-24.** The page layout's minimum is the **widest single collapsed group**, and the tab bar's horizontal size policy is `Ignored` — its preferred width is not a smaller number, it is not a number at all, since overflow is decided by `relayoutTabs()` from the width the ribbon actually got. The case now asserts the achieved width *before* asserting overflow, so the two failures can never again be confused. A horizontal scroll for a ribbon narrower than one group is the better answer and is real work: **carried to T11**. ✅ **Fully closed 2026-08-24 at T11**: the body sits in a `QScrollArea`, so the layout's minimum went back to the honest content figure — the SUM of the collapsed groups — while the window stayed narrowable, because a scroll area's own minimum is a frame and a scrollbar. Collapse is still exhausted before scrolling begins, which is the order that matters: a ribbon that scrolled while it still had groups left to collapse would be hiding content it could have shown. The scrollbar's height is **reserved at every width** rather than appearing and disappearing — a ribbon whose height depends on its width is R-4's flicker wearing a different hat |
| I-20 | §5.6 · A-54 · `RawPageAction` | **"Exactly one receiver on `triggered`" was the wrong invariant**, and all 192 ribbon actions said so at once. `QToolButton::setDefaultAction` adds the action to the button and Qt connects the button to `triggered` itself. Placing an action on a widget is the ordinary, correct thing to do with one — an invariant that forbids it forbids the ribbon. | The check bounds what is legitimate rather than asserting a number that was never true | **S3** | Found 2026-08-24 at T10, in the same run as I-18. The plan wrote the invariant as `directConnectionCount() == 1` (§5.4) and it was never exercised against an action on a button until the ribbon existed | **Closed 2026-08-24.** The invariant is *every receiver accounted for*: one from `ActionFactory`, plus at most one per widget in `associatedObjects()`. **The residual gap is recorded in `RawPageAction.h` rather than glossed** — an action on two widgets where only one connects could hide a single direct connection. It is a bound, not a proof, and it is still the only mechanism that can see a second `connect()` at all, which the lint structurally cannot |
| I-21 | §5.6 · A-54 · `ActionFactory` | **`ActionFactory` had never called `setCheckable`**, so `setChecked()` on every action it built was a no-op. The header has promised since T10 that *"a toggle is checkable, and only a toggle is"* — and the promise had never met a subject that could disprove it, because nothing registered a `checked` predicate until the Quick Access Toolbar's placement item at T11 | A toggle renders with a check mark; a non-toggle renders without one, and the two are distinguishable | **S3** | Register any command with a `checked` predicate and put it on a menu before the fix: the item is never ticked, whatever the predicate returns | **Closed 2026-08-24 at T11.** The factory now asks `ActionRegistry::isToggle()` — a new accessor, because `isChecked()` structurally cannot answer it: it returns false both for "not a toggle" and for "a toggle that is off", which is exactly the conflation the registry's own header warns about. `tooltipKey()` was added in the same change for the same reason: a `Registration` field nothing could read since T3. **The lesson is T10's I-18 and I-20 again in a third form** — a self-check, or a promise in a header, is only worth what its first meeting with a real subject proves |
| I-22 | §5.6 · A-54 · `QLineEdit` | **A third kind of Qt-manufactured action**, found by the wiring self-check the first time a shown window contained the tool finder: `setClearButtonEnabled(true)` builds a `QAction` for the clear button and the check reported `bare QAction "" (_q_qlineeditclearaction)`. Unlike `QMenu::menuAction()` and `QDockWidget::toggleViewAction()` it is **not** recognisable by identity — measured, not assumed: it goes on the line edit's private side-widget list, so `QWidget::actions()` does not contain it, and the obvious identity check finds nothing | Qt's own intrinsic actions are excused; ours are checked | **S3** | Put a `QLineEdit` with a clear button on a shown `MainWindow` in a debug build: `Q_ASSERT_X` in `showEvent` fires | **Closed 2026-08-24 at T11.** `ActionFactory::adoptWidgetActions(widget)` lets the widget's owner stamp what Qt made **at the moment the property is turned on** — it stamps what exists at that instant and nothing later, which is what keeps it from becoming a blanket amnesty. Matching Qt's internal object name was the alternative and was rejected: it ties a gate to a private constant. The residual gap is stated in the header |
| I-23 | §5.5 · A-58 · `file.open` | **A ten-minute CI hang.** T12 registered `file.open` with a modal `QFileDialog`, and `tst_shell`'s long-standing case *"the window registers exactly the actions it can service"* dispatches that id to assert it is `NotFound`. Under `offscreen` the dialog is built, shown to nobody, and never returns — the suite stopped dead at 274 of 275 tests with no failure and no output | A command a headless caller dispatches either runs or is **refused**; it never blocks | **S2** — a hang is worse than a failure, because a failure names itself | Before the fix: `ctest -R "the window registers exactly"` under `QT_QPA_PLATFORM=offscreen` never returns | **Closed 2026-08-24 at T12.** `file.open` carries an **enablement predicate** that is false when there is no user to show a dialog to (`platformName() == "offscreen"`), so a headless dispatch is refused with `Unavailable` — distinct from the `NotFound` of a command that does not exist, which is precisely the distinction T3 built those two codes for. The predicate is the fix rather than a check inside the handler because the registry already has the vocabulary, and a batch runner needs the answer, not the silence. **The parameterized form of "open this file" already exists** and needs no user: `MainWindow::openFiles()`, which is what flow B and the CLI use. The case that hung now asserts both codes — `NotFound` for `file.save`, `Unavailable` for `file.open` — so the hang has become an assertion |
| I-24 | §5.6 · A-53 · `ShortcutBinder` | **The first version of the text-field case asserted the opposite of what it meant.** It pressed `Ctrl+A` in a `QLineEdit` on a shown window and required the key event to come back **unaccepted** as proof that the shortcut layer had stood aside. The field accepts it — that is what a text widget does with `Ctrl+A` — so the case failed against a product that was behaving exactly as designed. Worse, it was **vacuous** either way: `edit.selectAll` is one of the ~341 ids Stage 1 declares and does not register, so no dispatch could have happened whether the handshake worked or not. | A case about a collision uses a chord that actually collides, and asserts the SHELL did not act rather than that nobody did | **S4** — a defect in the case, never shipped | Found 2026-08-24 at T13 on the suite's first run: 296 of 297 green, this one red for the right behaviour | **Closed 2026-08-24.** The case now registers `edit.selectAll` itself — which is what Stage 3 will do for real, and the moment that would otherwise have broken every line edit in the product — then asserts the handler did **not** run and the field **did** select its text. **I-18 and I-20's lesson in a fourth form:** a check meets its real subject once, and that is when it gets corrected; here the subject was a `QLineEdit` doing its job |
| I-25 | §5.6 · A-12 · `layer-lint` | **The `bare-qaction` rule flagged a diagnostic string.** T13's self-check produces the message *"… bypasses …"*, and the rule's patterns include `QAction` followed by a space — matched inside the message text, in a file that is not an allowed path. `checks` failed on prose. | A blunt, non-escapable substring rule is allowed to be blunt; the tree works around it | **S4** | Found 2026-08-24 at T13 by `gates.layer_lint.clean_tree` — which is the fixture that exists for exactly this: it asserts the real tree is clean rather than only that bad input fails | **Closed 2026-08-24 by rewording the MESSAGE, not the rule.** Narrowing a non-escapable rule to ignore string literals would weaken the one gate that stands between the product and an unroutable action, and would do it for a cosmetic reason; the reworded diagnostic costs nothing and carries a comment saying not to "fix" it back. **Recorded because the next person to write such a message will hit it too**, and because a gate that catches its own documentation is the failure mode Stage 0's I-20 named — here it is the harmless direction of it |
| I-26 | §5.10 · A-92 · `core/cmd/KeyBindingStore` | **A remap ADDED a chord instead of replacing one.** `rebind(chord, context, action)` inserted an override and left the action's §8 default binding resolving, so after moving `file.preferences` from `Ctrl+K` to `Ctrl+Alt+P` **both** keys opened Preferences. Worse than a cosmetic surplus: D1-10's persisted family is `shortcuts/<context>/<action>` = **one** chord, so only one of the two could ever be written, and a second rebind of the same action put two entries in `exportOverrides()` whose survivor depended on map order. The user's setting and the product's behaviour disagreed, and a restart resolved the disagreement arbitrarily. | An override says what an action's chord **is**, and a restart reproduces it exactly | **S3** | Found 2026-08-25 at T14 by building the Shortcuts page — **the first caller that ever remapped a chord for a user rather than for a test**. Every prior test rebound a chord and then asked what that chord did; none asked what happened to the *old* one. Invisible to `tst_keybindings` for exactly that reason | **Closed 2026-08-25.** An override now **supersedes** that action's default chords in the same context, in `resolveExact` and in `bindingsFor` alike so the keyboard and the Preferences page cannot disagree about which chords exist; `rebind` drops any previous override chord for the action first. The rule is **derived** from the overrides rather than stored, so nothing new is persisted and a `settings.ini` written before T14 means exactly what it meant. Two consequences are written into `KeyBindingStore.h` because neither is obvious: a chord freed this way falls through the resolution order like any unbound chord, and an action with two §8 defaults (`Ctrl+Y` and `Ctrl+Shift+Z` are both redo) loses **both**. `tst_toolfinder`'s A-56 case asserted the old behaviour in as many words and was rewritten to assert the new one, plus the half a chord-shaped assertion misses: that the superseded row stops answering |
| I-27 | §5.2 · `tools/i18n-lint` | **`orphan-key` was about to report forty keys nobody could act on.** T14's Preferences surface builds catalog keys from tables at runtime — one per settings path, one per choice-set member, one per `KeyContext`, one per page — so the whole key appears in no source and only the prefix does. Every one of them would have been reported as an orphan on every run, forever. | A note nobody can act on does not get written every run | **S3** | Found 2026-08-25 at T14 before the keys were added, by asking what the gate would say about a key that is never spelled out. It would not have failed a build — `orphan-key` is S3 by design — which is precisely the danger: forty non-blocking notes per run is how people learn to skip the whole rule | **Closed 2026-08-25.** A quoted key reference **ending in a dot** is now a *namespace* reference and covers every key beneath it. Two things kept it from being an escape hatch: the prefix has to be narrow, so `settingsLabelKey` produces `prefs.setting.<path>` rather than a bare `prefs.<path>` that would have switched the check off for the whole Preferences namespace; and the gate case asserts **both halves** — the generated key under the namespace is not reported **and** a key outside it still is. That second half needed a new `EXPECT_NOT_MATCH` in `tests/gates/RunGateCase.cmake`, because a suppression asserted only by the absence of a failure is equally satisfied by a gate that stopped reporting anything. The bare preset went 65 → **66** |
| I-28 | §5.10 · A-93 · `app/LogFile` | **The rolling log file could not name the binary that wrote it.** Everything logged before composition reached the console and not the file — including `CrashHandler::install()`'s build id, which is the one line a support bundle needs in order to symbolize a dump against an archived binary (D1-13). The file cannot exist any earlier: its retention count is `logging/retainFiles`, and `Settings` does not exist until `CompositionRoot` runs. | A log a user mails in can be matched to the build that produced it | **S3** | Found 2026-08-25 at T14 by **reading the file the executable actually wrote**, rather than by testing `LogFile` — which passes either way, because the omission is in the ordering of the process and not in the class | **Closed 2026-08-25** with a session banner: the first line of every log file names the version, the build id and the Qt version. Restating the build id was the right fix rather than deferring the crash handler's own line — the handler must be installed before anything else can go wrong, and a log whose first line is a session header is what every rolling log has anyway |
| I-29 | §5.9 · A-85 · `tests/bench/bench_ribbon.cpp` | **Every figure the new benchmark printed on its first run was 0.000 ms, and the bug was the benchmark's.** The three percentile calls were arguments to a single `printf`, and C++ leaves argument evaluation order unspecified — so the `std::move` on the last of them ran first and p50 and p95 read a moved-from vector. A benchmark reporting zero is not a benchmark failing; it is one agreeing with you. | A measured criterion is measured, rather than reporting a number that flatters it | **S3** (the harness, not the product) | Found 2026-08-25 at T15 by **disbelieving the result**: a 192-control ribbon cannot repaint in under a microsecond, and the printed p95 disagreed with a p95 computed one line above it. Nothing failed, and nothing would have | **Closed 2026-08-25.** The three percentiles go into locals before the `printf`. Two things were added in the same change so the class of error is visible rather than remembered: the run now prints **the size the ribbon actually got** alongside the size requested (T10's lesson as an instrument reading), and the `layout` column — still 0.000 ms, but now known to be a fact — carries **a control**, twenty resizes across the collapse threshold, which reports 0.714 ms and proves the instrument can be non-zero |
| I-30 | §5.9 · A-80 · `.github/workflows/ci.yml` | **The `windows` CI job had never run, and the cause was a variable NAME, not a missing runner.** A self-hosted runner (`recto-dev-win`, labelled `self-hosted / Windows / X64 / qt6111`) had been online since 2026-08-17. The repository variable that existed was `RECTO_HAS_WINDOWS_RUNNER`; the workflow reads `RAWPAGE_HAS_WINDOWS_RUNNER`. Every document in the set had read the unset variable as *"no runner yet"* and recorded a hardware absence that was not real. | The one job that covers Windows, the widget suite, the OS layer and the real-configuration gates actually runs | **S2** — for a week, three-quarters of the product's test surface had no CI at all, and the run page said nothing was wrong | Found 2026-08-25 at T15 by **querying the GitHub API for the runner instead of believing the comment in the workflow that explained why there wasn't one**. The comment was accurate when written and had simply stopped being true | **Closed 2026-08-25.** `RAWPAGE_HAS_WINDOWS_RUNNER=true` set with the user's authorization. The deeper defect is that two artefacts in different systems had to agree and nothing checked they did — so the fix is not only the variable: the new `coverage-report` job states in every run summary which jobs actually ran and, when the Windows one did not, enumerates precisely what went unexercised. A skipped job can no longer be read as a passing one. `RECTO_HAS_WINDOWS_RUNNER` is left in place rather than deleted; it belongs to whoever set it |
| I-31 | §7 · the caveat table | **The "do not report this as a bug" list contained a bug of its own.** Two of its eight rows were wrong when checked against the built product: the ribbon row still carried the plan's *forecast* of "~200 ids declared, ~60–80 registered" when the real figures are **358 and 19**, and the `Ctrl+Z` row said the chord was *"registered, dispatching, empty journal until Stage 3"*. It is not registered. `Ctrl+Z` is bound in `KeyBindingDefaults.def` and resolves, but `EditUndo` is not among the nineteen, so `dispatch` answers `NotFound` — and by T13's deliberate design a `NotFound` **leaves the key unconsumed**, which is why a `QLineEdit` keeps its own undo. There is no journal to be empty either; `CommandJournal` is Stage 3. | §7 is the list the manual walker checks an observation against, so a wrong row does not merely misinform — it **suppresses a real finding**, by telling the walker that what they just saw is expected | **S3** | Found 2026-08-25 at T15 when the user asked whether the guide held everything still outstanding. Answering that meant reading §7 as a walker would, and both rows failed against `grep`. Neither would have been caught by any run: §7 is prose, no test reads it, and the automated half was already green | **Closed 2026-08-25.** Both rows corrected against the source — the counts from `ActionId.def` and the registration sites in `MainWindow.cpp`, the chord from `KeyBindingDefaults.def` and the registry. `Stage1-AppShell-Plan.md` §11 carried the same wrong `Ctrl+Z` claim and was corrected in the same change, since it is where the forecast originated. **The general lesson is the one this stage keeps relearning from the other direction:** a document written *before* the code — which is the rule that makes these guides worth having — states expectations that later turn out false, and nothing mechanical ever revisits them. §7 in particular is written in the register of settled fact |
| I-32 | §5 · the whole automated half | **This guide's summary said 73 cases green while 46 of its own cases carried an unticked box and no recorded result — and two of those 46 could never have been run at all.** The results were real; they lived in `Implementation-Plan.md`'s per-task rows and never came back to the cases they belonged to, because a *task's* status block is what gets updated when a task finishes and this file is not. On top of that: **A-25** names a `rawpage.exe --list-actions` switch that has never existed in `app/CommandLine.cpp`, and **A-26** needs an alias fixture nobody wrote, for a `RAWPAGE_ACTION_ALIAS` mechanism that is implemented in `ActionId.h` and has **zero rows and no test**. Both had been counted as green since T3. | A green summary means every case underneath it is green. Otherwise the summary is the only thing anybody reads, and it is the one thing that is not evidence | **S3** | Found 2026-08-25 at T15 when the user asked whether everything outstanding was in this guide. Answering it meant cross-checking each case's header verdict against its own checkbox — which nothing had ever done, because the two markers are prose and no test reads either. **This is I-31 one level up**: that was two wrong rows in §7, this is the same failure across a whole section | **Closed 2026-08-25.** Every case re-run or re-verified against the built product and ticked **only on named evidence**, with §5 gaining a table saying which evidence covers which case. **A-25 and A-26 are deliberately left unticked** and marked in place with what other mechanisms *do* cover, and both are carried to Stage 2 — A-26 with a note that Stage 2 should plant a throwaway alias row rather than let a real rename in Stage 7 be the first exercise of that path, which is D1-16's own argument turned on itself. The counts in the header now say **71 of 73** and name the two. Two smaller errors fell out of the same pass: A-65's steps named a `-BuildId` parameter that resolves nothing (the real one is `-Binary`, and running it correctly is what produced the case's first recorded output), and the unregistered-chord arithmetic still said "~341" from before T14 registered two more commands — it is 339 |
| I-33 | M-03 · tear-off / `MainWindow::detachDocument` | **Dragging the ONLY tab out of a window detaches it into a new window and leaves the original with no tabs at all.** It is a null move dressed as a real one: the same lone document ends up alone in a window either way, and the user is left with a second, empty shell to close. `detachDocument` guards the *index* — `documentAt(index)` must exist — but never asks how many documents the window has, so the make-a-new-window branch fires at a count of one exactly as it does at a count of five. | Detaching the only document **into a new window** is refused and the drag does nothing — the window it would leave behind is the window it would create. Dropping that same lone tab **onto another window's strip** stays allowed: that one is a real move | **S3** | **Found by the user walking M-03, 2026-08-30.** No automated case could have caught it: `tst_documenttabs.cpp` drives `detachDocument` only from a two-document window, which is the count at which the behaviour is correct. **The severity departs from M-03's blanket ‘If it fails S2’ and is stated rather than assumed:** every operation in the case's Steps works, no later stage builds on it, and an empty window is a legitimate state — it is what a fresh launch is — so this is weird rather than wrong. Read it as S2 instead and say so: an open S2 blocks Stage 1's Definition of Done and an open S3 does not | **Closed 2026-08-30.** One guard in the new-window branch of `detachDocument`, placed **after** the drop-onto-another-strip loop, refusing at `documentCount() <= 1` and logging why. Two cases: `a window's only document is not detached into a new window`, which carries **its own control** — a second document makes the same call succeed, because a guard that refused everything would satisfy the refusal checks and be a worse defect than the one it fixes — and `a lone document can still be dropped onto another window's strip`, which exists to pin the **placement**. Both negative-controlled: removing the guard fails the first (4 assertions); moving it above the loop fails the second (4 assertions), which is the placement claim proved rather than asserted. The second case also **retires the manual-only note** on that branch — it needs no pointer, only the target strip's own `mapToGlobal` |
| I-34 | M-03 · re-dock / `DocumentTabBar::mouseMoveEvent` | **Re-docking a tab into another window cannot be performed by a person. It always makes a new window instead.** The branch exists and is correct — `detachDocument` adopts into whatever window's strip the pointer is over — but it is fed a position taken at the **single move event where the 40 px vertical threshold is first crossed**, not at release. `mouseMoveEvent` emits there and then sets `pressedTab_ = -1`, so a drag gets **one** attempt. The target strip is ~25 px tall and must be in the band beginning exactly 40 px clear of the source strip, sampled on that one event; an ordinary drag steps past it. The user tried repeatedly on real hardware and got a new window every time. | A tab dragged onto another window's tab strip docks into that window, at the drop position — the behaviour `MainWindow.h`, `Architecture-FileMap.md`, `CLAUDE.md` and **T12's own acceptance line** (*‘Tear a tab out and re-dock it’*, `Stage1-AppShell-Plan.md` §6) all describe as shipped | **S2** | **Found by the user walking M-03, 2026-08-30**, and it is the case's whole reason for existing: this is the one branch `tests/ui/tst_documenttabs.cpp` records as manual-only (*‘needs a pointer position over a real strip and stays manual’*), so nothing had ever driven it — not a test, not a person. The unit case that looks like coverage calls `adoptDocument` directly and proves the **destination** works, never that the gesture can reach it. **S2 rather than I-33's S3:** a documented behaviour is not merely odd, it is absent through the only affordance that exists, and Stage 4 builds window arrangement on top of it. This opens §9's *no open S1/S2* box | **Closed 2026-08-30.** `DocumentTabBar` now decides on **release**. At the threshold it still ends Qt's own move-drag — that part was always necessary and is unchanged — but it records the torn tab in `tearingTab_` and emits from `mouseReleaseEvent` with the release position; moves in between are swallowed so Qt cannot restart a drag it was told had ended, and the cursor becomes `DragMoveCursor` so the gesture is visible while it is carried. A left press while a tear is still recorded clears it, which is the recovery path for a lost grab. Case: `the tear-off gesture reports where the pointer was RELEASED`, whose load-bearing line is `CHECK(reports == 0)` **at the crossing** — negative-controlled by restoring the emit to `mouseMoveEvent`, which fails it 3 assertions to 2. **Not fixed by widening the 40 px threshold**, which would have traded an unreachable re-dock for a reorder that tears off by accident |
| I-35 | M-03 · re-dock / `MainWindow::closeIfEmptied` | **A window emptied by re-docking its last tab into another window stays open with no tabs in it.** Visible only once **I-34** made re-dock performable at all — tear a tab out, drag it back, and the window it came from is left behind as an empty shell to close by hand. | A window emptied by a **move** closes itself. A window emptied by a **close** does not: the user is still standing in it and may open the next file into it | **S3** | **Found by the user walking M-03, 2026-08-30**, in the same sitting as I-33 and I-34 and only reachable after both were fixed — which is the argument for re-walking a case after its defects are repaired rather than signing it off on the strength of the repair. Not a regression: the state was always possible, and nothing could reach it | **Closed 2026-08-30.** `closeIfEmptied()`, called from the adopt branch of `detachDocument` and nowhere else. **It closes only a window that OWNS ITSELF** — `WA_DeleteOnClose`, which this class sets on a tear-off and on `window.new` and on nothing else. The primary window is a **stack object** at `app/main.cpp:314` and `main()` captures its address for single-instance forwarding: Qt may not delete it, and `close()` would merely hide it, leaving a hidden entry in the window set for `Ctrl+F6` to walk into and for a forwarded launch to call `openFiles()` on. An empty primary window is the state a launch with no files produces, so stopping there is correct rather than a shortfall. Two cases, **each with its own control**: the round trip (tear out, drag back, the source window is gone) fails with the call removed, and `a window that does not own itself is emptied but not closed` — which passes with or without the fix and would be vacuous alone — fails when the ownership check is removed, i.e. it is a control against a fix that is too broad |

| I-36 | M-04 · the ribbon tab strip / `Ribbon::Ribbon` | **Not one ribbon tab is drawn.** The `QTabBar` is laid out **zero pixels wide at every window size**, so the strip shows only the tool finder and the body below it is whichever page happened to be current — Home. Every tab from Create to View, the chevron overflow behind them, and the pointer route to any control not on the Home tab are unreachable. | Nine tab labels in the strip, the current one selected, the rest one click away | **S2** | **Found by the user opening M-04, 2026-08-30** — the case's first step is *View tab → Panes → Log*, and there was no View tab to click. **Present since T10** and not a regression: `git log -L` puts `setSizePolicy(QSizePolicy::Ignored, ...)` in `dd28437`, the commit that shipped the ribbon. The T2 spike M-01 was walked against had the same `addWidget(tabs_, 0)` + `addStretch(1)` pair **without** that policy, which is why M-01 saw a tab strip and nothing since has | **Closed 2026-08-30.** `Ignored` says this widget's width preference is *not a number*, so QBoxLayout has nothing to give it unless a **stretch factor** states its share — and next to a separate `addStretch(1)` the spacer took the whole strip. The tab bar now carries the stretch itself and the spacer is gone; the corner widget is held right by an expanding tab bar rather than by a spacer. Measured either side of the change in a standalone probe: **0 px → 975 px** in a 1195 px strip, and 80 px — one tab — in a 300 px one. New case **`the tab bar is allocated real width so the tabs are on screen`** with the control convention 7 requires: reverted, it fails at `0 >= 72`. **Why eleven green ribbon cases missed it** — every one of them asked the tab bar what it *contained* (`pageCount`, `currentPage`, `isTabVisible`, `hasOverflow`), and `tabRect()` measures the bar's own contents, which stay correct in a widget the layout gave no width at all. Nothing asserted what the **layout handed the widget**. That is convention 19 in a second disguise: the harness proved the destination and never the route |

| I-37 | M-04 · auto-hide / `MainWindow::buildAutoHideStrips` | **An auto-hidden sidebar could be brought back only by CLICKING the edge strip.** Reaching for the edge — the gesture this case's *Expect* names and the one every editor with auto-hiding panes uses — did nothing. The put-away half was already right: `SidebarDock::leaveEvent` unpeeks. | The strip peeks when the pointer arrives on it | **S3** | **Found by the user walking M-04, 2026-08-30.** Not reachable by the widget suite as it stood: offscreen there is no pointer, and every auto-hide case called `peek()` directly — convention 19's back door again, this time with the door itself missing | **Closed 2026-08-30 by decision rather than by default.** Click-to-reveal (VS Code, the Windows shell) and hover-to-reveal (Visual Studio) are both defensible; **the user chose hover**, so the product moved to the guide rather than the guide to the product. `MainWindow` filters its two strips for `QEvent::Enter` and peeks — an event filter because `QToolButton` has no pointer-arrived signal and `enterEvent` is protected — and **the click still reveals**, because a strip that looks like a button and ignores a press would be a second defect. The filter is removed in `~MainWindow` for I-12's reason arriving through the event system. One case, `hovering the edge strip peeks the auto-hidden pane`, covering the wiring and the leave round trip; with the filter uninstalled it fails at `isPeeking() == false` |
| I-38 | M-04 · `general/touchMode` | **A preference a user can switch on that changes nothing.** The key is declared in `core/settings/SettingsKeys.def`, stored, and offered on the shipped General page since T14 — and the only occurrences of it in the tree are its own two rows. No hit target, font or margin reads it. | Either the switch does something, or nothing offers it | **n/a — not a defect** | **Found before the walk reached it, 2026-08-30**, by grepping the key while preparing M-04 rather than by pressing it. It was in neither out-of-scope list, so it was a gap in the documents rather than a decision anyone had taken | **Closed 2026-08-30 by a scoping decision, and recorded here so the decision is traceable to the walk that raised it.** The user's call: **planned absence, Stage 14** — touch mode is `Features.md`'s pen-and-finger work and belongs with the stage that owns the preference set, not bolted onto the shell to make one manual step pass. Now a row in this guide's §1 and in `Stage1-AppShell-Plan.md` §11, the key's §2 row says so, and M-04's *Steps* and *Expect* no longer ask for it. The alternative considered and rejected was hiding the key from the General page: the page is generated from the table, and carving one row out of a generated surface to hide an unfinished feature is the kind of special case D1-10 exists to prevent |

| I-39 | M-04 · auto-hide / `MainWindow::updateAutoHideStrips` | **The edge strip stays on screen beside the pane it exists to summon.** Hover the strip, the pane slides out — and the 24-pixel bar is still there next to it, so the window shows both the stand-in and the thing it stands in for. | The strip is up only while the pane is away | **S3** | **Found by the user re-walking M-04's hover step, 2026-08-30** — the step that existed because of I-37, on the build that fixed it. Older than both: the strip has behaved this way since T9, and nothing could see it because until I-37 no test and no person could peek a pane except by calling `peek()` in a fixture, where visibility is not what is being looked at | **Closed 2026-08-30.** `isAutoHidden()` stays true through a peek, on purpose — a peek is a look and not a pin — so on its own it was the wrong question for this function. The condition now excludes a peeking pane, and the strip returns on `unpeek()`. The pointer is standing on the strip when it goes, which is safe and is *why* the pane is reachable: the dock area sits outside the central widget, so the revealed pane takes the space the strip was drawn in and arrives under the pointer. Asserted in both directions inside `hovering the edge strip peeks the auto-hidden pane`; with the peek term removed it fails at `!true` |

| I-40 | M-05 · `ui/shell/ThemeManager` colour table | **In dark mode most of the shell's text is not readable.** Not the theme failing to apply — background, enabled labels, the heading and the status bar were all correct — but **disabled** text, which in this shell is nearly everything: **358 commands are declared and 19 registered**, so the disabled colour is what most of the ribbon is drawn in. Measured, `disabledText` on `windowBackground` was **3.05** dark and **2.54** light, and on `surfaceBackground` **2.62** dark. | Text a person can read in both themes, while still looking disabled | **S3** | **Found by the user walking M-05, 2026-08-30**, by switching Windows to dark with the app running. Present since T9. Invisible to every prior pass for two reasons worth separating: **M-01 and M-02 walked the LIGHT theme only** — nothing before M-05 had a reason to switch the OS — and the automated theme suite asserted that each token *resolves*, that light and dark *differ*, and that the background/text pair is the right way round, none of which is a question about whether the result can be read | **Closed 2026-08-30.** `disabledText` is now `#8a8a8a` dark and `#7a7a7a` light: **4.77 / 4.10** dark and **3.87 / 4.29** light against window and surface. **The light half was changed too, though only the dark half was reported** — it measured *lower* than the value that was called unreadable, and shipping a fix that leaves the worse number in place would be fixing the report rather than the defect. New case **`every text token is legible against the surface it is drawn on`**, which measures every foreground token against the background it is actually painted over, in both themes, against a floor of **3.0** — a floor rather than a target, so a deliberately quiet colour can stay quiet — and asserts in the same case that disabled text stays at **less than half** the enabled text's ratio, because raising the floor until the two are indistinguishable would trade this defect for a worse one. It measures RATIOS, never hex values: pinning the hex would make the case a copy of the table it checks. Control: with the old pair restored it fails on three of the seven, naming each ratio. **This is not an accessibility criterion** — accessibility left the product on 2026-08-23 — it is whether the ribbon can be read |

| I-41 | M-05 · `ui/shell/ThemeManager::apply` | **The dark theme was only half in force, and the half that was missing is the ribbon.** Qt's native `windows11` style paints control text from the colour scheme **it** derives from Windows and ignores the application palette while doing it, so setting a palette themed the `QLabel`s and left every `QToolButton` painting in the OS's colours. Measured on a light-mode OS with the dark palette installed: an **enabled** button's glyph came out `#1f1f1f` on a `#1f1f1f` background — invisible — and a **disabled** one `#181b1f`, *darker than the background*; a `QLabel` in the same window honoured the palette exactly (`#8a8a8a`). | The whole shell paints in the theme the user chose | **S2** | **Found by the user re-walking M-05 on 2026-08-31**, after I-40 changed the palette and the text was still unreadable — which is what said the palette was not the thing painting it. Present since T9. **I-40 was a real defect and its fix was not wrong; it was incomplete**, and the reason is worth keeping: the token table was the only place anyone had thought to look, so a measurement of the tokens confirmed the tokens and said nothing about what reached the screen. The step that found this was rendering a widget and reading the pixels back | **Closed 2026-08-31.** `apply()` now calls `QStyleHints::setColorScheme()` as well as installing the palette — `Light`/`Dark` for an explicit choice, **`Unknown` for follow-system**, which hands the scheme back to the OS. That last one is ordered before `systemTheme()` runs and the order is load-bearing: `systemTheme()` reads `styleHints()->colorScheme()`, so a manager that had just forced Dark would read its own answer back and report it as the system's — "follow system" would quietly mean "follow whatever I last chose". Re-measured after the fix: enabled `#99deff`, disabled `#3d5871`. `setColorScheme` emits `colorSchemeChanged`, which is connected to `apply()`, so there is a re-entrancy flag and a case that calls `apply()` twice. **What the suite cannot see, named rather than papered over (convention 19):** offscreen, `setColorScheme` is inert — `colorScheme()` still answers `Unknown` and no signal fires — so the case asserts the theme → scheme **mapping** and cannot assert that Qt was told, and it cannot assert painting at all, because the style offscreen is not the style on a desktop. The pixel evidence above is the control, and it was taken by hand |

| I-42 | M-06 · `Alt` key tips / `MainWindow` registration + `Ribbon::showKeyTips` | **Pressing `Alt` does nothing, in every locale.** The ribbon has *drawn* key tips since T10 — two levels, `Esc` backs out — and **nothing ever asked it to**: `Alt` is bound to `AppRibbonKeyTips` in `KeyBindingDefaults.def`, `ShortcutBinder` forms that chord and `tst_context` asserts it does, and the id was simply **never registered**, so `dispatch` answered `NotFound` and left the key unconsumed. Separately, the tips took **the first character of the tab title**, so in `zh-TW` they would have read 常 建 編 註 組 表 保 檢 — untypable — with **表 claimed twice**, by 表單 (Forms) and 表格工具 (Table Tools). | `Alt` raises the tips; a letter chooses a tab; `Esc` backs out one level | **S2** | **Found by the user opening M-06, 2026-08-31.** Present since T10. **`Tech-Stack-Plan.md` §3.3 recorded R-5 as ✅ Met, signed off 2026-08-23 via M-01** — and M-01 was walked at T2 against the throwaway spike, which had a handler of its own that the shipped ribbon never inherited. The same shape as I-36: a criterion judged against something that is not the product, then described as shipped. Every link was tested — the chord, the binding, the drawing — and the **chain** was not | **Closed 2026-08-31.** `AppRibbonKeyTips` is registered (**the twentieth** of 358 declared ids) and toggles the tips; the follow-up letter arrives in `Ribbon::keyPressEvent`, because `showKeyTips()` takes focus and gives it back on the way out — not a second application-wide key filter, which would put a rival beside `ShortcutBinder` and undo D1-7. The letter is now **catalog data**: `ribbon.keytip.<tabId>`, derived from the tab id the way T14 derives `prefs.setting.*` from a settings path, resolved by `RibbonTabBuilder`, which **reports a tab with no access key as a problem** rather than inventing a letter. Nine keys per locale, Latin in both, which is what Office does in Chinese; level two was already digits. Two cases, two controls: dispatch through the registry (**unregister it and it fails at the dispatch**) and `every tab access key is typable and no two tabs claim the same one`, asserted against **both shipped catalogs** — put 表 back for Forms and Table Tools and it fails naming both. The second case is the one that would have caught the half that was wrong only in Chinese, and it is asserted per locale because **M-01 walked English** |

| I-43 | M-11 · `QuickAccessToolbar` reorder | **Reordering the toolbar could not be done by a person.** `moveCommand(from, to)` has been public since T11 and `UiCustomization` has persisted the order for just as long — and there was **no menu item, no drag and no `ActionId`**, so the only caller in the tree was `tst_qat`, which calls the widget method directly. Three documents described reordering as shipped: `QuickAccessToolbar.h`'s own header comment (*"All five are here"*), `CLAUDE.md`, and `Architecture-FileMap.md`. | Reorder is reachable from the toolbar, like the other four operations | **S3** | **Found 2026-08-31 while answering the user's question about the QAT context menu during M-06** — the menu has four items and the fifth operation had nowhere to be. Logged against **M-11**, whose steps say *"customize the toolbar"*, rather than against M-06. This is **I-34's shape exactly**, and the third instance in this stage: the destination was proven by a test that called the method, and the route was never built. Convention 19 is the rule it breaks | **Closed 2026-08-31.** Two new ids — `ui.qatMoveLeft` and `ui.qatMoveRight`, **the only ids Stage 1 has added to the 358 the plan declared** (now 360) — in the context menu beside Remove, each with an enablement predicate so it is **disabled at the end it cannot move towards** rather than offered and silently refused. Commands rather than a drag, because a drag reaches nothing but a pointer and this toolbar's rule is that every operation is reachable from the batch runner too (flow K). The index is resolved at dispatch time from the pending target, never captured — convention 14, and O11 for why the target is a parameter the registry has no channel for. **The case asserts both halves, and each has its own control**: the dispatch moves the button (drop the predicate → the end-stop assertions fail) **and the real context menu offers the two items** (take them out of `buildMenu` → the route assertions fail, which is precisely the build that shipped from T11). A dispatch-only case would have passed against that build |

| I-44 | M-06 · `ui/shell/ToolFinder` result list | **A query that matched nothing froze the application.** The result list is a `Qt::Popup` **window**, so while it is up it holds the mouse grab for the whole process — and the *"nothing matched"* message is itself a **row**, so an empty result set still showed the list. Nothing could then put it away: `Esc` was not handled, a click outside was not handled, and `Enter` on an unselectable row returned early **without hiding**. Every click anywhere in the window went to a popup that ignored it. Force-closing the process was the only way out. | A list that can always be dismissed | **S2** | **Found by the user walking M-06's tool finder step, 2026-08-31**, by making a typo — which is how anybody would find it, and nobody had. Present since T11. **On this guide's scale it is an S2** (§1 reserves S1 for the architecture's load-bearing claims), and that undersells it: in ordinary terms a freeze needing Task Manager is the worst defect this stage has produced. Backspacing to an empty query would in fact have recovered — the list hides when the query is empty — but `Esc` and clicking away are what a person tries, and both did nothing | **Closed 2026-08-31.** Three exits, because three were independently missing: `Esc` (which puts the **list** away and leaves the typing alone), a mouse press the popup receives at coordinates outside itself — the grab is why it arrives at all, and this is exactly what `QMenu` does and a bare `QListWidget` with the popup flag does not — and a failed `Enter`. `FocusOut` and `WindowDeactivate` hide it too, so alt-tabbing cannot leave a grabbing popup over another application. One case, `the result list can always be put away`, asserting each exit separately; with the filter removed it fails on the `Esc` and outside-click exits, naming both. **The case also records a Qt behaviour it tripped over**: `setQuery` goes through `QLineEdit::setText`, which emits nothing when the text is unchanged, so re-typing the same query after a dismissal leaves the list hidden — the helper clears first, or the second half of the case would have passed for the wrong reason |

| I-45 | M-06 · `ui/shell/ToolFinder` — **a regression introduced by I-44's fix** | **No search showed any results at all.** The first version of the I-44 fix dismissed the list on any `FocusOut` of the search box — and **showing a `Qt::Popup` is itself a focus change**: the box receives a `FocusOut` with reason `PopupFocusReason` at the moment the list appears, so the list was closed in the same turn it opened. Typing 檢視 in the Chinese UI produced nothing, where it had produced hits the hour before. | The results show, and still dismiss on a real focus loss | **S2** | **Found by the user re-walking M-06's tool-finder step on the repaired build, 2026-08-31 — which is the entire argument for re-walking a case after its defect is fixed.** Shipped for exactly one build. The automated case added with I-44 could not see it: offscreen, showing the list causes no real focus change, so the assertion that the list is visible passed while on a desktop it was not | **Closed 2026-08-31.** `FocusOut` with `PopupFocusReason` is ignored — losing focus **to the popup** is not losing focus — and `WindowDeactivate` is honoured only on the popup itself rather than on the shell window behind it. The case now asserts the rule in both directions: a `PopupFocusReason` focus-out leaves the list up, a `MouseFocusReason` one puts it away. **Its control is the bug**: drop the reason check and the first of those two fails. The event is sent by hand because the platform behaviour it stands for does not occur offscreen, and the case says so — what is covered is the rule, not the platform |

---

## 9. Sign-off criteria

Stage 1 is signed off when **all** of the following hold — this is
[`Implementation-Plan.md` §2's Definition of Done](./Implementation-Plan.md#definition-of-done--every-stage-no-exceptions)
made concrete for this stage:

- [x] Every **automated** case A-01 … A-85 — plus **A-67**, added at T7, and **A-90 … A-94**, added
      at T14 — is ✅, run by me, with actual output reported. 🟡 **71 of 73, 2026-08-25 at T15**:
      334/334 on both Windows presets, 66/66 on the bare `gates` preset, each case ticked against
      named evidence. **A-25 and A-26 cannot run** — a switch that does not exist and a fixture that
      was never written — and are carried to Stage 2 rather than counted (**I-32**). This line is
      ticked because both exceptions are recorded and neither is a product defect; a reader who
      disagrees should treat the box as open, which is why the exceptions are named here rather than
      buried.
- [x] …**and re-run in CI at the final counts on all three jobs.** ✅ **2026-08-25**, run
      32785493072: four jobs green, **the `windows` job having run for the first time ever** — 334/334
      there, 52/52 on `gates`, 196/196 on `portable-asan`. **A-80 is closed**, and with it every
      automated case in this guide.
- [ ] Every **manual** case M-01 … M-12 is ✅, walked by you on the §2 rig, and **signed off by you**.
      I do not mark these.
- [ ] Every caveat in §7 has a human verdict, not an assumption. **This is part of your walk, not a
      separate task** — each row is something that will look wrong and is not, and the verdict is
      *"yes, I saw that and it is the documented behaviour"*. **Two of those rows were themselves
      wrong and were corrected at T15**, which is the argument for the checklist existing: the
      registered-command figures were a forecast nobody had re-measured, and the `Ctrl+Z` row
      described behaviour the product does not have.
- [x] No open **S1** or **S2** in the issue log. ✅ **2026-08-30** — the log runs I-01 … I-35 and
      none is open. This box was **re-opened and closed again the same day** by **I-34**, an S2 in
      the product: re-dock, which four documents and T12's own acceptance line describe as shipped,
      could not be performed by a person. It is left visible here rather than tidied away, because a
      box that had been ticked since 2026-08-25 was ticked over a feature nobody had ever driven —
      which is the argument for the manual half of this guide existing at all.
      The five S2s before it all closed in the task that found them: **I-08** and **I-09** at
      T5 (the settings codecs, found by fixtures written the way a person hand-edits an INI),
      **I-11** at T7 (a race in the single-instance bus, caught by the platform suite's first run),
      and **I-30** at T15 (the `windows` CI job that had never run). Every other entry is S3 or S4.
- [x] **The A3 verdict is recorded** in `Tech-Stack-Plan.md` §3.3, on the three-point scale, with a
      **per-criterion result for R-1 … R-9** rather than one overall impression. ✅ **2026-08-23 at
      T2: `go, with named custom work`**, per criterion, with **R-8 withdrawn rather than cleared**
      and the verdict explicitly resting on eight criteria rather than nine. **R-9 was re-measured at
      T15** against the shipped ribbon rather than the spike.
- [x] **The `-municode` question is answered** either way, in the Stage 1 status block, closing Stage
      0's deviation (a). ✅ **2026-08-23 at T1: not adopted**, by measurement — `argv` loses bytes the
      ANSI code page cannot represent, and `QCoreApplication::arguments()` does not, so the problem
      the flag exists to solve is solved for the only consumer we have. That measurement became
      convention 1 and issue **I-04**, and it is now held by three mechanisms rather than a comment.
- [x] `Architecture-FileMap.md` Stage 1 rows flipped ○ → ✅, and the rows
      [plan §10](./Stage1-AppShell-Plan.md#10-lockstep-debt-this-stage-creates) names as missing added.
      ✅ **2026-08-25 at T15** — no Stage 1 row is `○`. The rows still showing `🟡` are files a later
      stage extends (`CrashHandler` owes `SIGABRT`, `LogPane` and `PreferencesDialog` grow per stage),
      which is what `🟡` means. **Every file in the tree now has a row**, `scripts/symbolize.ps1`
      included — the oldest debt this stage carried, owed since T1.
- [x] `Implementation-Plan.md` Stage 1 status, §5's guide row, `CLAUDE.md`'s gate count, and
      `README.md`'s *Current state* updated to reality, deviations included. ✅ **2026-08-25 at T15.**

**That is the whole remainder.** Everything above except the two unticked lines is done, and both of
those are yours: the §6 manual cases, and the §7 caveat verdicts that come with them. Nothing else in
this stage is waiting on code, on CI, or on me.

*"Automated green, manual not yet walked" is a legitimate state to report for this stage, and given
the size of §6 it will be the state for a while. It is not "done".*
