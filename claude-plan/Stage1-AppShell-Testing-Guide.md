# Stage 1 — App Shell & Action Layer · Testing Guide

> **Authoritative verification document for Stage 1.** Written from
> [`Stage1-AppShell-Plan.md`](./Stage1-AppShell-Plan.md) and
> [`Implementation-Plan.md` § Stage 1](./Implementation-Plan.md#stage-1--app-shell-action-layer-settings-and-single-instance),
> **before any Stage 1 code exists** — which is the point: verification written afterwards only ever
> confirms whatever got built, including the parts built wrong.
>
> **Status:** ⚪ **Written 2026-08-19 · not yet run.** No case below has a result. **67 automated**
> cases (A-01 … A-85) and **12 manual** (M-01 … M-12).
>
> **One issue was raised and closed before a line of code existed** — **I-01**, found while writing
> case A-31 by parsing `Features.md` §8 rather than reading it. The "twelve deliberate collisions"
> figure turned out to be **correct but unscoped**; the scope is now stated in the owning document.
> See the [issue log](#8-issue-log). **No open issue.**
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
| A full screen-reader pass — R-8 here is a **reduced** check | Stage 14 |

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
| `general/theme` | `FollowSystem` | Light / dark / follow-system; overridden by OS high contrast (D1-15) | A-52, M-05 |
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
- **A screen reader** — Narrator suffices (**P-2**). This is a *reduced* pass for R-8; Stage 14 owns
  the real one.
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
| **A3** — can Qt Widgets host a real ribbon? | M-01 (R-1…R-5, R-7), M-02 (R-6), M-08 (R-8), A-85 (R-9) | `Tech-Stack-Plan.md` §3.3, beside A5 and A6 |
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
| The window opens with a functional ribbon | A-50, A-51, M-01 |
| **Every** button, menu item, and chord dispatches through `ActionRegistry` | A-12, A-20 … A-24, A-53, A-54, M-09 |
| A debug assertion catches a directly-connected `QAction` | A-12, A-54 |
| The §8 Global defaults all resolve correctly | A-30, A-33, A-34, M-08 |
| Settings round-trip | A-40 … A-46, M-11 |
| `--lang zh-TW` shows a translated UI | A-72, M-06 |
| A second launch reuses the first instance | A-60, A-61, M-07 |
| **Risk A3 has a recorded verdict** | M-01, M-02, M-08, A-85 |
| *(plan)* An undeclared settings key does not compile | A-41 |
| *(plan)* The ribbon builds entirely from a declarative resource | A-50, A-51 |
| *(plan)* `checks` runs three gates | A-10 |

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

**A-07 · A build id is embedded**
☐ **Goal** D1-13. **Steps** `objdump -s -j .note.gnu.build-id .\build\x64-release\rawpage.exe`
**Expect** A non-empty hex build id, and the same id recorded in the build's artifact manifest.
**If it fails** S2 — a minidump without a build id cannot be symbolized, which makes the crash handler
decorative.

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
☐ **Goal** ~200 declared, ~60–80 registered at this stage. The gap must be *visible*.
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

**A-52 · Theme tokens resolve for light, dark, and follow-system**
☐ **Expect** Every declared token has a value in each mode; no hard-coded colour in a widget.
**If it fails** S3. *(High contrast is M-05 — the OS must be asked, and offscreen has no OS to ask.)*

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

**A-58 · `Ctrl+O` does not sniff file contents**
☐ **Goal** D1-2. Deciding whether bytes are a PDF is `IDocumentBackend`'s job.
**Steps** Open a `.pdf` whose contents are plain text.
**Expect** A stub tab opens. No read of the file body anywhere in `ui/`.
**If it fails** S2 — a content check here pre-empts Stage 2's boundary, and boundaries are much harder
to reclaim than to hold.

### 5.7 Single instance, command line, crash handler

**A-60 · A second launch forwards and exits 0**
☐ **Steps** Launch `rawpage.exe`, then `rawpage.exe C:\path\to\a.pdf` from a second shell.
**Expect** The second process exits **0** quickly; the first opens a stub tab for the path.
**If it fails** S2. *(Raise-to-foreground is M-07 — offscreen has no foreground.)*

**A-61 · `general/singleInstance=false` gives two windows**
☐ **Expect** Two processes, two windows, neither forwarding. **If it fails** S3.

**A-62 · The mutex is `Local\`, not `Global\`**
☐ **Goal** D1-11's per-session scope. **Steps** Inspect the named object.
**Expect** `Local\`. **If it fails** S3 — a `Global\` mutex means the second user on a machine cannot
run the app at all, which is a support incident nobody will diagnose quickly.

**A-63 · `tst_commandline` — parse only**
☐ **Expect** Green: an open-with-file path, `--lang <locale>`, `--version`, an unknown flag (error
with a documented exit code), and an empty argv. **If it fails** S2.

**A-64 · The parser links without the executable**
☐ **Goal** Plan §5.7. It lives in the headless suite; if that stops being possible, the parser has
grown a dependency. **Expect** `rawpage_tests` builds and `rawpage_assert_headless()` still passes.
**If it fails** S2 — and the finding is about the parser, not the test.

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

**A-72 · `--lang zh-TW` actually loads the catalog**
☐ **Steps** Under `offscreen`, launch with `--lang zh-TW --dump-ui-strings`.
**Expect** A sampled set of strings differs from the `en` run, and **no string is the untranslated
English fallback**. This is a mechanical check of *loading*; whether the Chinese is any **good** is
M-06, and this case must not be reported as covering that.
**If it fails** S2.

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

**The A3 verdict is assembled from M-01, M-02, M-08 and A-85.** Please record a per-criterion result
(R-1 … R-9) rather than one overall impression, because "go with named custom work" is a pass and
without per-criterion results nobody can tell which work was named.

---

**M-01 · The ribbon is a real ribbon — A3's core (R-1, R-2, R-3, R-4, R-5, R-7)** ☐
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

**M-02 · Mixed DPI, and a scale change while running (R-6)** ☐
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

**M-05 · Theme follows the system, and yields to high contrast (D1-15)** ☐
- **Steps** With `general/theme=FollowSystem`, switch Windows between light and dark **while RawPage is
  running**. Then set RawPage to Dark explicitly and switch Windows again. Then turn on a Windows
  high-contrast theme.
- **Expect** Follow-system tracks live, with no restart and no half-themed widget. An explicit choice
  wins over the system. Under high contrast, **our theming yields to the OS palette** and the
  Preferences page shows `general/theme` as *overridden*, not as disabled and not as still-in-effect.
- **If it fails** S3 for a mis-tinted widget; **S2 if high contrast is ignored** — overriding a user's
  accessibility setting is a compliance defect, not a styling one.

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

**M-08 · Keyboard-only, and a reduced screen-reader pass (R-8)** ☐
- **Pre** P-2 — **Narrator, built into Windows; nothing to install.** `Win+Ctrl+Enter` starts and
  stops it. A screen reader is what a blind user drives the app with: it reads the focused control
  aloud, asking Windows' UI Automation layer what that control *is*. That makes it the only honest
  test of whether our ribbon controls exist to anything but the eye — a custom-painted widget can
  look perfect and report nothing at all, which is one of the two realistic routes to an **A3**
  no-go. Turn the volume down before you start; it begins talking immediately.
- **Steps** Unplug or ignore the mouse. Reach: every ribbon tab and control, the QAT, the tool finder
  (`Ctrl+Shift+U`), the sidebar (`F4`), the ribbon toggle (`F9`), each pane, both Preferences pages,
  and back out with `Esc`. Then repeat the ribbon walk with the screen reader on.
- **Expect** Nothing is unreachable; focus is always visible; tab order is sane; the screen reader
  announces a **name and a role** for each control rather than "button" or silence.
- **If it fails** **S1 against A3 if ribbon controls are structurally unreachable or unnamed** —
  that is the one realistic route to an A3 no-go. S3 for an individual missing name; Stage 14 owns the
  full accessibility pass and this is deliberately a reduced one.

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

| # | Case / area | What happens | Expected | Sev | Repro / notes | State |
|---|---|---|---|---|---|---|
| I-01 | A-31 / A-32 · `Features.md` §8 | The **"twelve deliberate collisions"** figure could not be reconciled with §8. A tree-wide census gives **6** chords where any context redefines a `Global` chord, **9** where a context overrides a `Common` one, **15** for the two together, and **52** chords appearing in more than one table — none of them twelve. | The quoted figure matches a census of the tables it refers to, under a stated definition | **S3** | Found 2026-08-19 while writing this guide, **before any Stage 1 code**, by parsing §8 rather than reading it. Pre-existing — the figure predates this stage | **Closed 2026-08-20. The figure was right; it was never *scoped*.** Twelve counts **Write mode's** collisions specifically: **8** overriding Viewer (`Ctrl+B` `Ctrl+E` `Ctrl+L` `Ctrl+R` `Ctrl+U` `Ctrl+=` `Ctrl+Shift+=` `Ctrl+Shift+N`) and **4** overriding Global (`Ctrl+D` `Ctrl+K` `Ctrl+Shift+A` `Ctrl+Shift+L`), 0 overriding Common. The clue was Stage 13's own wording, *"the twelve deliberate collisions with the Viewer context"* — right that Write mode is the scope, wrong that Viewer is the only counterpart. Scope now stated in [`Features.md` §8](./Features.md#what-the-twelve-deliberate-collisions-counts); Stage 13's sentence and `CLAUDE.md`'s corrected; A-31 asserts the 12, A-32 the 6 and 9 separately |

---

## 9. Sign-off criteria

Stage 1 is signed off when **all** of the following hold — this is
[`Implementation-Plan.md` §2's Definition of Done](./Implementation-Plan.md#definition-of-done--every-stage-no-exceptions)
made concrete for this stage:

- [ ] Every **automated** case A-01 … A-85 is ✅, run by me, with actual output reported — and re-run
      in CI at the final counts on all three jobs.
- [ ] Every **manual** case M-01 … M-12 is ✅, walked by you on the §2 rig, and **signed off by you**.
      I do not mark these.
- [ ] Every caveat in §7 has a human verdict, not an assumption.
- [ ] No open **S1** or **S2** in the issue log. **I-01 is closed** (S3, resolved 2026-08-20), so
      A-31 and A-32 now have defined figures to assert.
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
