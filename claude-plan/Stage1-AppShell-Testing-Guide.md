# Stage 1 — App Shell & Action Layer · Testing Guide

> **Authoritative verification document for Stage 1.** Written from
> [`Stage1-AppShell-Plan.md`](./Stage1-AppShell-Plan.md) and
> [`Implementation-Plan.md` § Stage 1](./Implementation-Plan.md#stage-1--app-shell-action-layer-settings-and-single-instance),
> **before any Stage 1 code exists** — which is the point: verification written afterwards only ever
> confirms whatever got built, including the parts built wrong.
>
> **Status:** 🟡 **In progress — Stage 1 T1…T9 verified as of 2026-08-23.** **68 automated** cases
> (A-01 … A-85, plus **A-67** added at T7) and **12 manual** (M-01 … M-12), of which **M-08 is
> withdrawn** with the accessibility removal.
>
> | | |
> |---|---|
> | **Automated, green** | A-03…A-08, A-10…A-11, A-13…A-17, A-20…A-26, A-30…A-37, A-40…A-47, **A-52**, **A-60**, **A-63**, **A-64**, A-62, A-65…A-67, A-70…A-72. `ctest` **226/226** on both presets; the bare `gates` preset **59/59** with no Qt; `checks` clean on **three** gates at 100 files / 509 includes, i18n-lint 45 files / 0 waivers |
> | **Automated, half run** | **A-58** — the acceptance half is green (a `.pdf` full of plain text is accepted, and nothing reads it); the `Ctrl+O` half needs T13's chords and T12's stub tab, and is not being claimed |
> | **Automated, not yet reachable** | A-12, A-50…A-51, A-53…A-57, A-61, A-80…A-85 — everything needing the ribbon, chord input, document tabs, or a CI run at the final counts. They arrive with T10…T15 |
> | **Manual** | **M-01 ✅** and **M-02 🟡** walked and signed off by the user 2026-08-23. M-03…M-07, M-09…M-12 are **not walkable yet** — the code they test does not exist |
>
> **Issue log: I-01 … I-17. None open** — I-04 closed at T8, I-12 … I-16 at T8/T9, and **I-17 by the
> first `portable-asan` run to reach Stage 1's code**. **Twelve of the seventeen were found by running
> something rather than by reading it**,
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
| `Ctrl+Z` / `Ctrl+Y` do anything visible — they dispatch to an **empty journal** | Stage 3 |
| `mutation-lint` — **three** of the four gates exist after this stage | Stage 3 |
| Contextual ribbon tabs with real content; the mechanism only is proven | Stages 5–13 |
| Session restore of *documents*; window geometry only | Stage 4 |
| Preferences beyond General and Shortcuts — 26 groups exist, three pages ship | Stage 14 |
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
.\build\x64-release\rawpage.exe                            # a window opens — this is new
```

**What "launched" looks like at this stage:** a window with a ribbon, an empty document tab bar, a
sidebar dock, and a status bar. `Ctrl+O` opens a **stub tab** — a filename and a grey placeholder
canvas, with no page on it. That is D1-2 working correctly, not a rendering failure.

### Settings keys that affect these cases

The twelve keys Stage 1 introduces
([plan of record](./Implementation-Plan.md#stage-1--app-shell-action-layer-settings-and-single-instance)).
This table replaces Stage 0's environment-variable table, and every one of these is a thing a case may
have to change and **must change back**.

| Key | Default | Effect | Cases |
|---|---|---|---|
| `general/theme` | `FollowSystem` | Light / dark / follow-system. *(OS high-contrast deference withdrawn 2026-08-23 with D1-15.)* | A-52, M-05 |
| `general/uiFontScale` | `1.0` | UI font scale, 0.75–2.0, clamped on out-of-range | A-43, M-04 |
| `general/touchMode` | `false` | Larger hit targets | M-04 |
| `general/restoreSession` | `true` | Reopen window layout on launch | A-44, M-11 |
| `general/singleInstance` | `true` | Whether a second launch forwards or opens its own window | A-60, A-61, M-07 |
| `language/uiLocale` | system | Which catalog loads; `--lang` overrides for one run | A-72, M-06 |
| `shortcuts/<context>/<action>` | §8 defaults | Per-context remaps (key **family**, D1-10) | A-35, A-36, M-09 |
| `logging/level` | `Info` | Log verbosity; the rule that document text never appears above debug | A-45 |
| `logging/subsystems` | all | Per-subsystem filtering | A-45 |
| `logging/retainFiles` | 5 | Log rotation depth | A-45 |
| `documents/recentCount` | 10 | Recent-list length | A-46 |
| `documents/recentPinned` | empty | Pinned entries survive eviction | A-46 |

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
| The window opens with a functional ribbon | A-50, A-51, M-01. 🟡 **The window opens as of T9**; the ribbon is T10, so this stays 🟡 and A-50/A-51 stay unrun |
| **Every** button, menu item, and chord dispatches through `ActionRegistry` | A-12, A-20 … A-24, A-53, A-54, M-09. 🟡 **True of everything that exists at T9** — the sidebar's close button dispatches `view.toggleSidebar` rather than calling `hide()`, and `ui.toggling the sidebar goes through the registry` asserts it. The *enforcement* (A-12's lint rule, A-54's self-check) arrives with `ActionFactory` at T10, and chords at T13 |
| A debug assertion catches a directly-connected `QAction` | A-12, A-54 |
| The §8 Global defaults all resolve correctly | A-30, A-33, A-34. ✅ **All green 2026-08-23 at T4**, with D1-9's document parse negative-controlled in both directions |
| Settings round-trip | A-40 … A-47, M-11. ✅ **A-40 … A-47 green 2026-08-23 at T5**, and the round trip closed at **T8**: nothing had ever called `Settings::sync()` (**I-14**), so every preference was correct for the life of the process and gone at the end of it — found by running flow B and then reading the file. M-11 needs the QAT (T11). A-41 is asserted three ways plus a positive control |
| `--lang zh-TW` shows a translated UI | A-70, A-71, A-72, M-06. 🟡 **A-70, A-71 and A-72 all green 2026-08-23** — A-70/A-71 at T6 and re-run at T9 (zh-TW key-complete at **389** keys; no unwrapped literal, 0 waivers), **A-72 at T8/T9**: 380 strings dumped per locale, 377 differ, 0 render a raw key. **The automated half is complete; M-06 is not** — it is the editorial review of the 28 entries marked unfinished, which `lrelease` was measured at T9 to *keep* rather than drop, so they do reach a reviewer's screen |
| A second launch reuses the first instance | A-60, A-61, A-67, M-07. 🟡 **A-67 green at T7 and A-60 green at T8** — a second launch forwarded `測試-📄.pdf` and exited 0, and the primary logged the path byte for byte across three process boundaries. **A-61** (`general/singleInstance=false` gives two windows) is not yet run and **M-07** (foreground activation) has no offscreen equivalent, so this criterion is **not** complete |
| **Risk A3 has a recorded verdict** | M-01, M-02, A-85. ✅ **Recorded 2026-08-23: `go, with named custom work`** *(M-08 withdrawn with R-8)* |
| *(plan)* An undeclared settings key does not compile | A-41. ✅ **Green 2026-08-23 at T5** — a misspelled key, a key named by string, and a right key with a wrong-typed value each rejected, with `declared-key.cpp` compiling as the control |
| *(plan)* The ribbon builds entirely from a declarative resource | A-50, A-51 |
| *(plan)* `platform.h` includes no Win32 header | ✅ **Green 2026-08-23 at T7**, and it is a `layer-lint` layer entry rather than a convention — `gates.layer_lint.platform_header_includes_windows` proves the rule fires, and `…platform_impl_may_include_windows` proves `platform/win/` may still include the OS |
| *(plan)* `checks` runs three gates | A-10. ✅ **Green 2026-08-23 at T6** — and A-11 confirms they still run on the bare `gates` preset with no Qt |

---

## 5. Automated verification — I run this

Every case names its exact command and its pass condition. I report the **actual** output; a failing
case is reported as failing, with that output, and is never declared fixed without a re-run.

### 5.1 Build, entry point, environment

**A-01 · Configure and build, both presets**
☐ **Goal** Stage 1's sources compile under the pin with `-Werror` in `core/`.
**Steps** `cmake --preset x64-debug` then `--preset x64-release`, build both.
**Expect** Exit 0 on all four. No warning in `core/`, `pdf/`, or `platform/`.
**If it fails** S1 — nothing else runs.

**A-02 · The `ui/` warning grace list is empty**
☐ **Goal** D1-12. **Steps** Read `RAWPAGE_UI_WARNING_GRACE` in `cmake/RawPageWarnings.cmake` after the build.
**Expect** Empty. Every entry present is a finding **with a named reason and an issue number**, or it
does not belong there. **If it fails** S3 — but note that a grace list that starts populated has no
baseline to shrink from, so an unexplained entry here is worth an argument, not a shrug.

**A-03 · The executable starts and exits cleanly under `offscreen`**
☐ **Goal** The property every later stage's headless smoke test depends on.
**Steps** `$env:QT_QPA_PLATFORM="offscreen"; .\build\x64-release\rawpage.exe --version`
**Expect** Exit 0, a version string on stdout, no window, no hang. **If it fails** S2.

**A-04 · Imports are still msvcrt, never ucrtbase**
☐ **Steps** `objdump -p .\build\x64-release\rawpage.exe | Select-String "DLL Name"`
**Expect** `msvcrt.dll` present; `ucrtbase.dll` **absent**; Qt6 Widgets/Gui/Core and the GCC runtime
DLLs present. **If it fails** S1 — the Stage 0 pin has regressed at the moment `ui/` was added.

**A-05 · Non-ASCII argv survives to `QCoreApplication::arguments()` — the D1-3 measurement**
☐ **Goal** Settle Stage 0's deferred `-municode` question by measuring rather than assuming.
**Steps** Launch `rawpage.exe --print-args` with a path containing CJK characters *and* one containing
an emoji, from PowerShell and from `cmd.exe`.
**Expect** The arguments come back **byte-for-byte correct in both shells**, with **no** `-municode`
in the flag set. **If it fails** this is not a bug — it is the *other* verdict: record that
`-municode` (or `Qt6::EntryPointPrivate`) is genuinely required, adopt it, and re-run. **The finding
must be written down either way**; an unrecorded pass here silently re-opens a Stage 0 deferral.

**A-06 · DPI awareness is set before any Qt object exists**
☐ **Goal** Per-monitor-v2 must be established before the first `QGuiApplication`, or Qt caches the
wrong value and M-02 fails for a reason nobody can see.
**Steps** Read `app/main.cpp`; confirm the awareness call precedes every Qt construction. Then
`rawpage.exe --print-dpi-awareness`.
**Expect** Reports `PerMonitorAwareV2`. **If it fails** S2 — and M-02 will fail confusingly.

**A-07 · A build id is embedded, and all three copies of it agree**
☐ **Goal** D1-13. **Steps** `objdump -s -j .buildid .uildd-release
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
☐ **Steps** `cmake --build --preset x64-release --target checks`
**Expect** Clean. Report the file and include counts; they will be several times Stage 0's 27 / 93.
**If it fails** S1 if a real violation exists.

**A-09 · `core/` still builds with no display and no engine**
☐ **Goal** The claim `portable-asan` exists to prove, now that `core/cmd/` and `core/settings/` are real.
**Steps** `cmake --preset portable-asan` and build.
**Expect** Green. **If it fails** S1 — something in the new `core/` code reached for QtWidgets or the
platform layer.

### 5.2 The gates — three of them now

**A-10 · `checks` runs three gates and says three**
☐ **Goal** The comment in `RawPageChecks.cmake` currently says two exist. After T6, three do.
**Steps** `cmake --build --preset x64-release --target checks`
**Expect** Exit 0; `layer-lint`, `license-audit`, **and `i18n-lint`** each report; the target still
**compiles no layer library**. **If it fails** S1 on a real violation; **S2 if `checks` dragged the
whole build in** — that defeats "the gates run first", which is the property the whole CI ordering
rests on.

**A-11 · `checks` still runs on the bare Linux runner**
☐ **Goal** D0-1 extended to the third gate — `i18n-lint` must link nothing but the standard library.
**Steps** `cmake --preset gates` then `--target checks` on a machine with no Qt.
**Expect** Green. **If it fails** S2 — a gate needing the build environment is a gate that runs last.

**A-12 · A bare `QAction` outside `ActionFactory` fails the gate — D1-7(a)**
☐ **Goal** The static half of "nothing is wired directly to a widget slot".
**Pre** Fixtures under `tests/gates/fixtures/layer-lint/qaction-*/`, **one per spelling**:
`new QAction(...)`, a stack `QAction a(...)`, `menu->addAction("Literal", ...)`,
`toolbar->addAction(icon, "Literal", ...)`, and — as a **negative control** — the legitimate
construction inside `ui/shell/ActionFactory.cpp` plus a comment mentioning `QAction` in prose.
**Steps** `ctest --preset x64-release -R gates.layer_lint.qaction`
**Expect** Every offending spelling fails with the named diagnostic; both controls pass.
**If it fails** **S1.** This is I-15 restated: a rule tested against one spelling is a rule that only
catches one spelling, and that finding cost Stage 0 its third S1.

**A-13 · `i18n-lint` fails on an unwrapped literal, in every spelling**
☐ **Pre** Fixtures covering: a raw `"…"` passed to `setText`, a `QStringLiteral("…")`, a `u"…"`, a
literal built by `+` concatenation, a literal passed straight to `setWindowTitle`, and **negative
controls**: a literal inside a comment, a literal that is an object name, and a properly-wrapped
`tr("…")`.
**Steps** `ctest --preset x64-release -R gates.i18n_lint.literal`
**Expect** Every offender fails with its diagnostic and its file:line; every control passes.
**If it fails** **S1** if a real spelling passes clean.

**A-14 · `i18n-lint` fails on a key missing from a shipped locale**
☐ **Pre** A fixture where `rawpage_en.ts` has a key `rawpage_zh_TW.ts` lacks.
**Expect** Fails naming the key and the locale. **If it fails** S1.

**A-15 · A locale marked `"shipped": false` is not checked — and says so**
☐ **Goal** D1-14. `zh-CN` and `ja` are declared and deliberately unshipped; the gate must skip them
**with a named reason**, not silently.
**Steps** `i18n-lint --explain`
**Expect** The output lists `zh-CN` and `ja` as declared-not-shipped, naming Stage 15.
**If it fails** S2 — a silent skip is indistinguishable from a forgotten locale, which is exactly the
distinction D1-14 exists to keep visible.

**A-16 · An `ActionId` with no `action.*` catalog entry fails**
☐ **Expect** Fails naming the id. **If it fails** S2 — an unlabelled action renders as a blank button.

**A-17 · The waiver census counts applied waivers only**
☐ **Goal** Stage 0's I-20, transplanted. A waiver marker inside documentation must not be counted.
**Pre** Fixtures: a real waiver on a code line; a marker quoted in a comment; a marker for an unknown
kind; a marker on a line with no code.
**Steps** `i18n-lint --explain` on the real tree and on the fixture tree.
**Expect** The real tree's census matches the number of waivers actually applied in product code, and
the three non-waivers are not counted. **If it fails** S3 — but the census is how anyone notices
waivers creeping from a handful to a hundred, so an untrustworthy number defeats the escapability.

**A-18 · `license-audit` still clean; no new dependency**
☐ **Steps** As Stage 0's A-12. **Expect** No SPDX identifier outside the allowlist; `Qt6::Charts`
still absent from the link closure. **If it fails** S1 (license).

**A-19 · `THIRD-PARTY.md` is unchanged by this stage**
☐ **Goal** Stage 1 adds no third-party dependency. **Steps** `git diff --exit-code THIRD-PARTY.md`
after a full `checks` run. **Expect** No diff. **If it fails** S3 unless a dependency really was
added — in which case it needs a decision, not a diff.

### 5.3 The action layer — `tst_actionregistry`

**A-20 · Register, dispatch, enablement**
☐ **Steps** `ctest --preset x64-debug -R tst_actionregistry`
**Expect** Green. **If it fails** S1.

**A-21 · An unknown id returns an `Error`, does not assert, does not no-op**
☐ **Goal** Plan §5.1. The batch runner will dispatch ids from a user's file.
**Expect** `dispatch(unknown)` returns an error `Result`. **If it fails** **S1** — a silent no-op
becomes a batch run that reports success and did nothing, discovered at Stage 14.

**A-22 · Disabled and unknown are *distinct* error codes**
☐ **Expect** Two different codes, both with `tr()`-able message keys. **If it fails** S2 — the CLI
cannot tell a user "that does not exist" from "that is not available right now".

**A-23 · Double registration is an error, not an overwrite**
☐ **Expect** The second `register_` returns an error and the first handler survives.
**If it fails** S2 — two stages claiming one id should fail loudly at startup, not race.

**A-24 · Every registered id has a label key, and every declared id has a default context**
☐ **Goal** Generated from `ActionId.def` (D1-6), so this is really a test that the generator and the
table agree. **Expect** No id with an empty label key. **If it fails** S2.

**A-25 · The declared-versus-registered split is reported, not hidden**
☐ **Goal** **327** declared (not the ~200 this case originally estimated — see the T3 deviation in
`Implementation-Plan.md`), ~60–80 registered at this stage. The gap must be *visible*.
**Steps** `rawpage.exe --list-actions`
**Expect** Every id listed with its state: registered, or declared-with-owning-stage.
**If it fails** S3 — but this list is what stops a later stage silently forgetting to register
something it declared.

**A-26 · A renamed `ActionId` still resolves through its alias — D1-16**
☐ **Goal** Saved shortcuts and QAT entries key off the *string* id, so a rename in a later stage
must not silently discard a user's customization.
**Pre** A fixture with `RAWPAGE_ACTION_ALIAS("old.string.id", NewEnumerator)`.
**Steps** Write a `shortcuts/` entry and a QAT entry under the **old** string, then load.
**Expect** Both resolve to `NewEnumerator`; the alias is reported by `--list-actions`; **no alias row
is ever removed** — the fixture asserts that deleting one breaks the load, which is the property that
keeps aliases permanent.
**If it fails** S2 — a rename that silently drops customizations is discovered by a user, not by us.

### 5.4 Key bindings — `tst_keybindings`

**A-30 · The §8 tables load, row for row**
☐ **Goal** D1-9. The test parses `Features.md` §8 and asserts the store matches every row.
**Steps** `ctest --preset x64-debug -R tst_keybindings`
**Expect** Green, and the test reports the number of rows parsed per context. On a parse failure it
must name **the row it could not parse**, not just fail.
**If it fails** S2 if the store is wrong; **S3 if the parser is wrong** — and the distinction matters,
so the failure output must make it obvious which.

**A-31 · The twelve Write-mode collisions — the figure the document set quotes**
☐ **Goal** [`Features.md` §8](./Features.md#what-the-twelve-deliberate-collisions-counts) scopes
"twelve" to Write mode. **Stage 13's exit criteria depend on this number**, so it is asserted here,
at the stage that builds the binding store, rather than discovered eleven stages later.
**Expect** The census reports exactly **12**: **8** overriding Viewer — `Ctrl+B` `Ctrl+E` `Ctrl+L`
`Ctrl+R` `Ctrl+U` `Ctrl+=` `Ctrl+Shift+=` `Ctrl+Shift+N` — and **4** overriding Global — `Ctrl+D`
`Ctrl+K` `Ctrl+Shift+A` `Ctrl+Shift+L`. Zero overriding Common.
**If it fails** S2, and the failure must say **which side moved** — a §8 edit, or the census.

**A-32 · The wider census — the two numbers that are *not* "twelve"**
☐ **Goal** Keep the three counts distinct. Conflating them is exactly what made the documented
figure look wrong before I-01 was resolved.
**Expect** **Class 1** — any context redefining a `Global` chord — exactly **6**: `Ctrl+D`, `Ctrl+K`,
`F11`, `Ctrl+Shift+L`, `Ctrl+Shift+A`, `Ctrl+Shift+G`. **Class 2** — a context overriding a `Common`
chord — exactly **9**: `Ctrl+X`, `Ctrl+C`, `Ctrl+V`, `Ctrl+A`, `Delete`, `Backspace`, `Esc`, `F2`,
`Ctrl+F`. §8's Common heading permits class 2 explicitly, so those are **not** shadows and
`shadows()` must not report them.
**If it fails** S2 — a shadow report that includes the permitted overrides is noise, and a noisy
report gets ignored.

**A-33 · Each class-1 collision resolves to the right id per context**
☐ **Goal** The one thing the whole `KeyContext` model exists for.
**Expect** `Ctrl+K` → Preferences in `Viewer`, → Insert-hyperlink in `WriteMode`. `F11` → enter in
`Global`, → leave in `FullScreen`. And so on for all six **class-1** chords (A-32) **and all twelve
Write-mode chords** (A-31). **If it fails** **S1** — this is the model.

**A-34 · Resolution order is innermost, then Common, then Global**
☐ **Expect** A chord defined in all three resolves to the innermost.
**If it fails** S1.

**A-35 · A remap persists and takes effect**
☐ **Expect** Round-trips through `shortcuts/<context>/<action>`; the new chord resolves and the old
one stops. **If it fails** S2.

**A-36 · A remap that shadows a Global chord is reported, not forbidden**
☐ **Goal** §8 ships six class-1 collisions by design; forbidding shadowing would make the specified
defaults illegal. **Expect** `shadows()` lists it; the remap succeeds. **If it fails** S2.

**A-37 · A shortcut key for a removed `ActionId` is dropped with a log line**
☐ **Goal** D1-10's family validator. **Expect** Dropped at load, one log line, no startup failure.
**If it fails** S3.

### 5.5 Settings — `tst_settings`, `tst_recentfiles`

**A-40 · Typed round-trip and defaults**
☐ **Steps** `ctest --preset x64-debug -R tst_settings` **Expect** Green. **If it fails** S1.

**A-41 · An undeclared key does not compile**
☐ **Goal** D1-10 — the guarantee that stops settings sprawl across sixteen stages.
**Steps** A negative-compile fixture (`try_compile`) naming a key that does not exist.
**Expect** Compilation **fails**. **If it fails** **S1** — if there is any way to name a key that was
never declared, the schema is a convention, not a guarantee, and it will be gone by Stage 6.

**A-42 · `changed(Key)` fires only on an actual change**
☐ **Expect** `set` to the current value emits nothing. **If it fails** S2 — from Stage 2 this signal
invalidates the render cache, and the resulting stutter will be blamed on the renderer.

**A-43 · Out-of-range values are clamped and logged, not rejected**
☐ **Steps** Hand-edit `settings.ini` to `general/uiFontScale=9.0`; launch.
**Expect** Starts, clamps to 2.0, logs it. **If it fails** S2 — a hand-edited INI that prevents
startup is a support call with no diagnosis path.

**A-44 · A malformed settings file does not prevent startup**
☐ **Steps** Truncate `settings.ini` mid-line; launch. **Expect** Starts on defaults with a log line.
**If it fails** S2.

**A-45 · Logging keys take effect, and document text never appears above debug**
☐ **Goal** `core/Log.h`'s standing rule, now that there is something to log.
**Expect** At `Info`, no file path and no document text in the log. **If it fails** **S1 (privacy).**

**A-46 · Recent files: note, pin, evict, clear**
☐ **Expect** `documents/recentCount` bounds the list; a **pinned** entry survives eviction; `clear()`
leaves pins per the specified semantics. **If it fails** S3.

**A-47 · Schema version: migration up, and no rewrite on downgrade — D1-16**
☐ **Goal** The three persisted vocabularies this stage creates must survive a later version changing
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

**A-50 · The ribbon builds entirely from `res/ribbon.json`**
☐ **Steps** `ctest --preset x64-release -R ui.tst_ribbonbuild`
**Expect** Every tab, group, and control in the JSON is present in the widget tree, and **no control
exists that the JSON did not declare**. **If it fails** S2.

**A-51 · An unknown action id in `ribbon.json` fails the build**
☐ **Goal** D1-8. **Steps** Build with a deliberately misspelled id.
**Expect** **The build fails**, naming the id and the JSON line. Not a warning; not a missing button.
**If it fails** S2 — the moment a typo yields a silently missing button, the next person writes widget
code and the declarative ribbon is dead weight.

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

**A-53 · `activeContext()` over synthetic focus chains — `tst_context`**
☐ **Goal** D1-5. Parented, never-shown widget trees; the innermost widget declaring a context wins;
a chain declaring none falls back to `Global`.
**Expect** Green over every `KeyContext` value, plus a nested case (Comments pane inside a Viewer)
and a detached-widget case. **If it fails** **S1** — this function is what makes one chord mean two
things, and every collision case above is downstream of it.

**A-54 · The no-direct-connection self-check catches a planted violation**
☐ **Goal** D1-7(b). A self-check nobody has seen fail is a self-check nobody knows works.
**Steps** `ctest --preset x64-release -R ui.tst_actionwiring`
**Expect** Two cases: the real window passes; a fixture window with one bare `QAction` and one
`RawPageAction` carrying a second `connect` **fails, naming both**.
**If it fails** **S1** — and note the lint (A-12) cannot catch the second one, which is exactly why
there are two mechanisms.

**A-55 · QAT customization round-trips**
☐ **Expect** Add, reorder, move above/below, reset; serialize and reload identically.
**If it fails** S3. *(Surviving a real restart is M-11.)*

**A-56 · Tool finder ranking**
☐ **Expect** An exact name ranks above a prefix, which ranks above a fuzzy subsequence; each result
carries its current chord from `KeyBindingStore`; a disabled action is shown as disabled rather than
hidden. **If it fails** S3.

**A-57 · A stub `DocumentWindow` holds no document state**
☐ **Goal** D1-2's guard against calcification. **Expect** The class has no `Document`, no
`CommandJournal`, and no member Stage 2 would have to *remove* rather than fill in.
**If it fails** S3 now, S2 at Stage 2 — which is why it is checked now.

**A-58 · `Ctrl+O` does not sniff file contents** — 🟡 **half run 2026-08-23 at T9**
☑ **Goal** D1-2. Deciding whether bytes are a PDF is `IDocumentBackend`'s job.
**Steps** Open a `.pdf` whose contents are plain text.
**Expect** A stub tab opens. No read of the file body anywhere in `ui/`.
**If it fails** S2 — a content check here pre-empts Stage 2's boundary, and boundaries are much harder
to reclaim than to hold.
**Result** The *acceptance* half is green: `ui.opening a file checks the name and never reads the
bytes` writes a file whose contents are `this is plain text, not a PDF at all`, names it `.pdf`, and
asserts `MainWindow::openFiles()` accepts it and notes it in the recent list. A companion case
asserts the two refusals — a path that does not exist and one that is not named `.pdf` — reach the
recent list not at all. **The other half is not runnable yet and is not being claimed:** there is no
`Ctrl+O` until T13 wires chords and no stub tab until T12, so this is re-run at T12 and the case
stays 🟡 until then.

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

**A-61 · `general/singleInstance=false` gives two windows**
☐ **Expect** Two processes, two windows, neither forwarding. **If it fails** S3.

**A-62 · The mutex is `Local\`, not `Global\`** — ✅ **automated at T7**
☐ **Goal** D1-11's per-session scope. **Steps** `ctest --preset x64-release -R platform.the_mutex`
(was: inspect the named object).
**Expect** `Local\`. **If it fails** S3 — a `Global\` mutex means the second user on a machine cannot
run the app at all, which is a support incident nobody will diagnose quickly.
**Note added at T7:** the pipe needs the same scrutiny and does not get it from the same mechanism.
`Local\` scopes a mutex and **not** a named pipe, so the pipe name carries the session id
explicitly; `platform.the_pipe_name_carries_the_session_id` asserts it.

**A-67 · The single-instance bus delivers a handoff** — *new at T7*
☐ **Goal** The mechanical half of A-60, runnable before `rawpage.exe` can forward anything (T8).
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

**A-65 · A forced crash writes a symbolizable minidump**
☐ **Goal** D1-13, the whole point of building this path now rather than at Stage 15.
**Steps** `rawpage.exe --crash-test` (debug builds only), then
`scripts\symbolize.ps1 -Dump <path> -BuildId <id>`.
**Expect** A `.dmp` in `%LOCALAPPDATA%\RawPage\crashes\`, and the script resolves the faulting frame to
a **function name**, not a bare address.
**If it fails** **S2** — a crash handler that has never symbolized a real dump is a crash handler that
will not work the first time it matters, and the first time is Stage 2.

**A-66 · The crash path runs its last rites**
☐ **Goal** The recovery-journal flush hook. `RecoveryJournal` is Stage 3, so Stage 1 installs the hook
with a no-op sink.
**Expect** The hook is invoked before the dump is written, and the fact that it is a no-op is
**recorded in the log**, not implied by silence. **If it fails** S3.

### 5.8 Internationalization

**A-70 · `zh-TW` is key-complete against `en`**
☐ **Steps** `ctest --preset x64-release -R gates.i18n_lint` on the real tree.
**Expect** Zero missing keys. **If it fails** S2.

**A-71 · No unwrapped literal in the real tree**
☐ **Expect** Clean, with the waiver census printed and small. **If it fails** S2, and each waiver
needs a reason a person wrote.

**A-72 · `--lang zh-TW` actually loads the catalog** — ✅ **2026-08-23 at T8/T9**
☑ **Steps** Under `offscreen`, launch with `--lang zh-TW --dump-ui-strings`.
**Expect** A sampled set of strings differs from the `en` run, and **no string is the untranslated
English fallback**. This is a mechanical check of *loading*; whether the Chinese is any **good** is
M-06, and this case must not be reported as covering that.
**If it fails** S2.
**Result** Green. **380 strings per locale** — 22 shell keys plus 358 action labels, and *not* a
sample: the dump is generated from `ui/shell/ShellKeys.def` and `ActionId.def` themselves, so a key
that reaches the screen and not the dump is impossible rather than unlikely. **377 differ; 0 render
a raw catalog key.** The three identical rows are named rather than rounded away:
`shell.window.title` and `shell.placeholder.heading` are both the product name, and
`shell.status.pageCountUnknown` is an em dash that zh-TW currently sets the same way — all three are
data a reviewer can change at M-06, which is the point of their being catalog entries at all.
**Measured rather than assumed, and it corrected a belief:** `lrelease` *keeps* a message marked
`type="unfinished"` as long as it has text, so the 28 provisional zh-TW terms do reach the running
product and are exactly what M-06 reviews. Only an **empty** translation is dropped. `en` is still
installed underneath every locale, for the day a key is added to `en` and not yet to zh-TW.
**This case says nothing about whether the Chinese is any good.**

### 5.9 CI, portable, and the one benchmark

**A-80 · CI green on all three jobs**
☐ **Expect** `gates`, `windows`, `portable-asan` all green; the new counts recorded here and in the
status block. **If it fails** S1.

**A-81 · `checks` still runs before any compile in CI**
☐ **Steps** Read the workflow log ordering. **Expect** The gate step precedes the build step in every
job that has both. **If it fails** S2.

**A-82 · `rawpage_ui_tests` runs on Windows and *only* on Windows**
☐ **Goal** D1-4. **Expect** Present in the `windows` job, absent from `gates` and `portable-asan`.
**If it fails** S2 — adding Qt Widgets to a Linux job to chase coverage quietly redefines what
`portable-asan` proves, and nobody would notice.

**A-83 · The headless assertion still holds**
☐ **Goal** The pressure D1-4 exists to resist. **Expect** `rawpage_tests` links neither `rawpage_ui` nor
`rawpage_platform`, directly or transitively. **If it fails** **S1.**

**A-84 · Every prior stage's cases still pass**
☐ **Steps** The whole Stage 0 gate suite. **Expect** Green at its Stage 0 counts plus the new i18n
cases. **If it fails** S1.

**A-85 · R-9 — a ribbon tab switch fits in one frame**
☐ **Goal** The one A3 criterion that is measured rather than judged.
**Steps** `tests/bench` tab-switch benchmark, 100 iterations, on the reference machine.
**Expect** p95 ≤ 16 ms. Report the actual figure, not a verdict.
**If it fails** S3 and it feeds A3's verdict as *go with named custom work* — a slow tab switch is a
painting problem with known fixes, not evidence that Qt Widgets cannot host a ribbon.

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

**M-02 · Mixed DPI, and a scale change while running (R-6)** 🟡

> 🟡 **Walked and signed off by the user, 2026-08-23 — for the RIBBON, which is all that exists.**
> **R-6 is met** and A3's DPI question is answered. Recorded as 🟡 rather than ✅ only because step 2
> names *"the sidebar, a floating pane, and Preferences"*, and at T2 none of them exists — `SidebarDock`
> and `ThemeManager` arrive at **T9**, `LogPane` and `PreferencesDialog` at **T14**. **This case must be
> re-walked after T14** for its non-ribbon half. That is a scoping fact about what was on screen, not a
> qualification of the user's verdict: what could be tested passed.
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

**M-03 · Document tabs: tear off, re-dock, and back** ☐
- **Goal** The structural behaviour D1-2 exists to let us test now rather than at Stage 4.
- **Pre** Three stub tabs open via `Ctrl+O` (any three `.pdf` files; nothing is parsed).
- **Steps** Reorder by dragging. Middle-click one closed. Tear one out to a new window. Drag it back
  and re-dock it, at the start and then at the end of the strip. `Ctrl+Tab` / `Ctrl+Shift+Tab` through
  them; `Ctrl+F6` / `Ctrl+Shift+F6` between windows. Open enough tabs to overflow the strip.
- **Expect** Every operation works and the tab's identity and title follow it. `Ctrl+F6` reaches the
  torn-off window. Nothing crashes when the last tab in a torn-off window closes.
- **If it fails** S2. Note whether the failure is in the *tab* or in the *window* — Stage 4 owns
  window arrangement, and a defect there is not this stage's to fix.

**M-04 · Panes dock, float, auto-hide, and remember** ☐
- **Steps** Dock the Log pane left, then right. Float it; move it to the second monitor; re-dock it.
  Auto-hide it and bring it back from the tab strip. Resize it, close the app, reopen. Then set
  `general/touchMode` true and `general/uiFontScale` to 1.5 and repeat the resize.
- **Expect** Width is remembered; auto-hide reveals on hover and hides on focus loss; touch mode gives
  visibly larger targets; the font scale does not clip labels.
- **If it fails** S3, unless a pane cannot be re-docked at all — S2.

**M-05 · Theme follows the system** ☐
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

**M-06 · The `zh-TW` UI is right, not merely present** ☐
- **Goal** P-3. `i18n-lint` can prove every key exists. Only you can see that a label is wrong, reads
  like machine output, or does not fit.
- **Steps** Launch `rawpage.exe --lang zh-TW`. Walk every ribbon tab, the QAT, the tool finder, the
  status bar, the sidebar, and both Preferences pages. Look specifically for: truncation and
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

**M-11 · Everything survives a restart** ☐
- **Steps** Customize the QAT; move it below the ribbon; resize and auto-hide a pane; set a theme; set
  a font scale; remap a chord; open three tabs; pin one recent file. Quit properly. Relaunch. Then
  quit by **killing the process** and relaunch again.
- **Expect** After a clean quit, every customization is back and the window geometry is restored.
  After a kill, **the same** — settings are written when changed, not at exit. Documents are **not**
  restored: `SessionState` is Stage 4, and their absence is correct here.
- **If it fails** S2 if a customization is lost after a **clean** quit; S3 after a kill.

**M-12 · The stage's honest emptiness is legible** ☐
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
| Many ribbon buttons are visibly disabled | ~200 ids declared, ~60–80 registered. The tooltip should say which kind of disabled — if it does not, **that** is the bug (M-12) |
| `Ctrl+Z` does nothing | Registered, dispatching, empty journal until Stage 3 |
| `checks` runs three gates, not four | `mutation-lint` needs `Command`, Stage 3. If it ever *claims* four, that is the bug |
| `portable-asan` does not run any UI test | D1-4, deliberate. The portable subset is `core/` + `pdf/cos/` by rule |
| No golden-image or round-trip suite ran | They have no subject until Stages 2 and 3 |
| `zh-CN` and `ja` are missing | D1-14, declared `"shipped": false`. A-15 asserts the gate *says so* |
| A second user on the machine gets their own instance | D1-11's `Local\` scope, deliberate |

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

---

## 9. Sign-off criteria

Stage 1 is signed off when **all** of the following hold — this is
[`Implementation-Plan.md` §2's Definition of Done](./Implementation-Plan.md#definition-of-done--every-stage-no-exceptions)
made concrete for this stage:

- [ ] Every **automated** case A-01 … A-85 — plus **A-67**, added at T7 — is ✅, run by me, with
      actual output reported, and re-run in CI at the final counts on all three jobs.
- [ ] Every **manual** case M-01 … M-12 is ✅, walked by you on the §2 rig, and **signed off by you**.
      I do not mark these.
- [ ] Every caveat in §7 has a human verdict, not an assumption.
- [ ] No open **S1** or **S2** in the issue log. **I-01 is closed** (S3, resolved 2026-08-20), so
      A-31 and A-32 now have defined figures to assert. **I-02 and I-03 closed 2026-08-23** at T1;
      **I-05 and I-06 closed 2026-08-23** at T2, both raised by your M-01 walk; **I-07 closed** at T3;
      **I-08 and I-09 closed 2026-08-23** at T5, both S2, both found by running the fixtures rather
      than by reading the code; **I-10 and I-11 closed 2026-08-23** at T7, the second an S2 race the
      new platform suite caught on its first run; **I-04 is open** (S3) and closes at T8 with the
      `argv` rule asserted by a case.
- [ ] **The A3 verdict is recorded** in `Tech-Stack-Plan.md` §3.3, on the three-point scale, with a
      **per-criterion result for R-1 … R-9** rather than one overall impression.
- [ ] **The `-municode` question is answered** either way, in the Stage 1 status block, closing Stage
      0's deviation (a).
- [ ] `Architecture-FileMap.md` Stage 1 rows flipped ○ → ✅, and the rows
      [plan §10](./Stage1-AppShell-Plan.md#10-lockstep-debt-this-stage-creates) names as missing added.
- [ ] `Implementation-Plan.md` Stage 1 status, §5's guide row, `CLAUDE.md`'s gate count, and
      `README.md`'s *Current state* updated to reality, deviations included.

*"Automated green, manual not yet walked" is a legitimate state to report for this stage, and given
the size of §6 it will be the state for a while. It is not "done".*
