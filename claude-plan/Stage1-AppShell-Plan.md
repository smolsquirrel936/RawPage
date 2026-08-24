# Stage 1 — App Shell, Action Layer, Settings, Single Instance · Execution Plan

> **What this document owns.** The **execution order** of Stage 1 and the **Stage-1-local design
> choices** that no other document decides. It owns no product facts. Every stack, layering,
> key-binding, and verification fact is cross-referenced to its owner and never restated as a new
> decision:
>
> | For… | Read |
> |---|---|
> | The Stage 1 objectives, components, key bindings handled, settings keys, and exit criteria | [`Implementation-Plan.md` § Stage 1](./Implementation-Plan.md#stage-1--app-shell-action-layer-settings-and-single-instance) |
> | The authoritative key-binding tables and the Preferences group list | [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference), [§9.1](./Features.md#91-preferences-groups) |
> | What each file is for, and flows A, B, J | [`Architecture-FileMap.md` §3.1, §3.3, §3.8, §3.12, §3.13, §5](./Architecture-FileMap.md#31-app--composition-root) |
> | The locked toolchain, the seventeen platform interfaces, the inward-only rule | [`Tech-Stack-Plan.md` §4.2, §4.3, §6](./Tech-Stack-Plan.md#43-what-qt-gives-us-for-free-versus-what-needs-platform-code) |
> | How Stage 1 is verified | [`Stage1-AppShell-Testing-Guide.md`](./Stage1-AppShell-Testing-Guide.md) — written from this plan, **before** the code |
>
> **Status:** 🟡 **In progress — T1 … T13 complete as of 2026-08-24; T14 is next.** Written
> 2026-08-19, immediately after Stage 0 signed off and before any Stage 1 code existed. This file is a
> plan, not a progress log — Stage 1's live status, task by task and deviation by deviation, is the
> Stage 1 status block in `Implementation-Plan.md`, and that block is the authority.
>
> **Amended since it was written, and each amendment is marked in place rather than silently applied:**
> **D1-15** (high contrast) and precondition **P-2** (a screen reader) are **withdrawn**, along with
> A3 criterion **R-8**, because accessibility was removed from the product on 2026-08-23 — see
> [`Features.md` → *Removed from scope*](./Features.md). **A3 is settled: `go, with named custom
> work`.** T7's interface count was written here as **fifteen** and is **seventeen** — see the T7
> row in §6 and `Tech-Stack-Plan.md` §4.3, which owns the figure. The original counted table rows,
> and one row declares two interfaces.

---

## 1. What Stage 1 is actually for

Stage 1 ships no document capability either. Nothing can be opened, parsed, rendered, or changed.
What it ships is the **shape every later stage is poured into**, and there are exactly four things
that get permanently harder if this stage gets them wrong:

1. **One dispatch path for all input.** If a single ribbon button is wired to a widget slot, the
   headless batch runner (flow K, Stage 14) can never reach that feature, and nobody will notice for
   thirteen stages. → `ActionRegistry`, plus a gate rule that makes the violation a build failure
   rather than a code-review question (§3 D1-7).
2. **One typed settings store, before there are twenty places to put a setting.** Settings sprawl is
   not something you clean up later; it is something you either prevent in the stage that introduces
   the first key or live with forever. → `SettingsSchema`, where an undeclared key does not compile.
3. **The A3 verdict.** Can Qt Widgets host a real ribbon — sizing, collapse, overflow, contextual
   tabs, key tips, per-monitor DPI — without a commercial control library? This is a **go/no-go**,
   and the fallback (a first-party ribbon widget) is a *Stage 1* cost if we learn it now and a
   *Stage 6* catastrophe if we learn it then. → the timeboxed spike in §5.3.
4. **Strings out of code, from the first string.** A `tr()` discipline retrofitted at Stage 14 is a
   sweep of seventeen stages' worth of literals. Retrofitted never, it is a product that cannot ship
   in `zh-TW`. → `tools/i18n-lint`, the third of the four gates.

Everything else in this plan serves those four.

**What Stage 0's manual pass should change about how this stage is built.** Stage 0 produced 22
findings and all three S1s came from manual cases the harness structurally could not reach — two
documentation gaps that made a clean-machine build unreproducible, and a gate that passed on real
violations because every fixture used the same spelling. The lesson is not "write more manual cases";
it is **vary the spelling in fixtures, and write the manual half to look for what the harness cannot
see**. Applied here: the ribbon and DPI cases in the guide are manual because no offscreen run can
judge them, and the key-binding cases are automated *against the source tables themselves* (D1-9)
rather than against a hand-copied list — a hand-copied list is I-15's single-spelling problem wearing
a documentation costume.

---

## 2. Inherited ledger — what Stage 0 hands over

Recorded so the tasks below are concrete. None of these is re-decided here.

| Fact | State at the end of Stage 0 | Consequence for Stage 1 |
|---|---|---|
| `rawpage.exe` | Links four layers, GUI subsystem, opens no window, exits 0 | T1 replaces `main()` for real; the exit-0 property stops being the test |
| `-municode` | **Deferred to Stage 1** by Stage 0 deviation (a) — `-mwindows -municode` with a plain `main()` fails to link (*undefined reference to `wWinMain`*) | **T1 resolves it by measurement**, not by adding a stub to satisfy a flag. See D1-3 |
| Gates live | **Two** — `layer-lint`, `license-audit`. `RawPageChecks.cmake` says so in a comment | T6 makes it **three**; `mutation-lint` stays Stage 3 |
| `tests/unit/` | 27 unit tests, linked against `rawpage_core` + `rawpage_pdf` **only**, with `rawpage_assert_headless()` asserting the resolved link closure | That assertion is load-bearing — it is what the `portable-asan` job mechanically proves. Stage 1 must **not** weaken it to fit a widget test. See D1-4 |
| `ui/` warning grace list | `RAWPAGE_UI_WARNING_GRACE` exists in `cmake/RawPageWarnings.cmake`, currently unused — there is no `ui/` code | Stage 1 is the first real `ui/` code. D1-12: it starts **empty** |
| `core/` vocabulary | `Result.h`, `Log.h`, `Geometry.h`, `Units.h` ✅ | `Result<T>` at every boundary introduced here; `Log.h` gets its first real subsystems |
| `layer-lint` rules | `core/settings/` already carries the filesystem-write privilege as its own layer entry | The settings store's writes are legal on day one; no rules change needed for them |
| CI | Three jobs — `gates` (ubuntu, no Qt) 25, `windows` (self-hosted) 66, `portable-asan` (ubuntu) 52 | T15 adds the `ui` suite to the `windows` job **only**. The Linux jobs never gain Qt Widgets |
| I-10, I-12 fixes | `cmake/ToolchainPreflight.cmake` runs before `project()` | Nothing to do; noted so the file is not mistaken for drift |
| Open findings | **None.** 22 entries, none open | Stage 1 starts from a clean issue log |

---

## 3. Stage-1-local decisions

Choices this plan makes because nothing upstream decides them. Each is scoped to Stage 1 and listed
here so a later stage can find and revisit it rather than rediscovering it.

| # | Decision | Why | Revisit |
|---|---|---|---|
| **D1-1** | **A3 is attacked with a timeboxed throwaway spike (T2) that produces a written verdict before any production ribbon code exists.** The nine acceptance criteria are fixed in §5.3 *before* the spike starts. | This is how Stage 0 settled A5 and A6, and it worked — both came back with detail nobody predicted (PDFium links no CRT at all). A go/no-go answered by preference rather than by a running program is not a verdict, it is a hope with a table row. Fixing the criteria first is what stops the spike being graded against whatever it happened to achieve. | Never — the *verdict* may be revisited; the method should not |
| **D1-2** | **A Stage 1 `DocumentWindow` is a stub holding no `Document`.** `Ctrl+O` accepts a real path, checks only that the file **exists** and ends in `.pdf`, and opens a tab whose canvas is an explicit "Stage 2 renders this" placeholder. **No content sniffing, no parsing, no `core/doc/`, no PDFium.** | The shell's genuinely hard parts — tab tear-off and re-dock, pane auto-hide and width memory, per-window state, the Explorer → single-instance → `openFiles` handoff (flow B) — are *structural*, and they are cheap to get right now and expensive to reshape once a real `Document`, a `CommandJournal` and a `PageView` hang off them. The alternative (an empty tab bar) leaves all of it unit-tested only and slips its manual cases to Stage 4. **No sniffing** specifically: deciding whether bytes are a PDF is `IDocumentBackend`'s job, and pre-empting that boundary inside `ui/` is the exact violation this architecture exists to prevent. | Stage 2 replaces the placeholder canvas with `PageView` and the path check with `IDocumentBackend::open()` |
| **D1-3** | **The `-municode` deferral is resolved by measurement, and the plan of record is that we do *not* adopt it.** T1 measures whether a non-ASCII command-line path survives to `QCoreApplication::arguments()` with a plain `int main(int, char**)`. | Qt on Windows sources `QCoreApplication::arguments()` from `GetCommandLineW()`, not from `argv`, so the Unicode problem `-municode` exists to solve may already be solved for the only consumer we have. Adding the flag and inventing a `wWinMain` to satisfy it is exactly the "stub to satisfy a flag" Stage 0 refused to write. If the measurement says otherwise, the fallback is Qt's own `Qt6::EntryPointPrivate`, which supplies the entry point without us hand-rolling one. **The measurement is the deliverable either way** — the flag's absence must be a recorded finding, not an omission. | Never, once measured and recorded |
| **D1-4** | **A second test target, `rawpage_ui_tests`, under `tests/ui/`. `tests/unit/`'s `rawpage_assert_headless()` is not touched.** `rawpage_ui_tests` links `rawpage_core` + `rawpage_ui`, runs under `QT_QPA_PLATFORM=offscreen`, and registers with CTest under the `ui.` prefix. | The headless assertion is the standing proof that `core/` is testable with no display and no engine, and it is what `portable-asan` verifies. The first widget test is precisely the pressure that would otherwise "just add `rawpage_ui`" to that target and destroy the property while every test still passes. Two targets keeps both truths. **`rawpage_ui_tests` never runs on `portable-asan`** — the portable subset is `core/` + `pdf/cos/` by rule. | Never |
| **D1-5** | **`ShortcutBinder::activeContext()` lives in `ui/` and is tested in `rawpage_ui_tests` against synthetic focus chains — not in `tests/unit/`.** | [`Implementation-Plan.md`](./Implementation-Plan.md#stage-1--app-shell-action-layer-settings-and-single-instance) calls it "a pure function of the focus chain … unit-tested with synthetic chains — no window needed", which is right about *no window* but a focus chain is made of `QWidget`s, so the test needs QtWidgets. Under `offscreen`, a parented never-shown widget tree suffices. Recording this stops someone satisfying the sentence literally by moving focus logic into `core/`, where `QWidget` is a denied external. | Never |
| **D1-6** | **`ActionId` is generated from one declarative table, `core/cmd/ActionId.def`** — an X-macro list of `RAWPAGE_ACTION(Enumerator, "action.string.id", DefaultContext, "i18n key")`. The enum, the string↔id maps, the default-context map, and the `action.*` catalog key list all come from that one file. | Three things must agree forever: the enum, the ribbon JSON, and the translation catalog. Three hand-maintained lists agree until the first Friday. One table makes a typo a build error at the point of the typo. It also gives `res/ribbon.json` something to be validated *against* (D1-8) and `i18n-lint` something to check `action.*` completeness against. | Never; later stages append rows |
| **D1-7** | **Two mechanisms enforce "nothing is wired directly to a widget slot", one static and one dynamic.** (a) A new `layer-lint` token rule makes constructing a bare `QAction` outside `ui/shell/ActionFactory.cpp` a gate failure, `escapable: false`. (b) `RawPageAction : QAction` is the only subclass, carries its `ActionId`, and exposes its `triggered` receiver count; a debug-build self-check walks the window's actions at show time and asserts every one is a `RawPageAction` with exactly one receiver. | The exit criterion asks for "a debug assertion that no `QAction` carries a direct connection". A runtime assertion alone only fires on a code path someone ran; a lint rule catches the construction at the moment it is written — Stage 0's own lesson about gates versus review discipline. Neither alone suffices: the lint cannot see a `connect()` on a legitimately-created action, and the assertion cannot see an action on a dialog nobody opened. | Stage 3, when `mutation-lint` arrives and may absorb rule (a) |
| **D1-8** | **`res/ribbon.json` is validated at build time against `ActionId.def`.** An unknown action id, a duplicate placement, or an empty group fails the build. | A declarative ribbon earns its keep only if adding a command in Stage 9 is a registry row plus a JSON line. The instant a typo yields a silently missing button instead of an error, the next person writes widget code, and the mechanism becomes dead weight that still has to be maintained. | Never |
| **D1-9** | **`tst_keybindings` parses `Features.md` §8 itself and asserts the store matches it, row for row.** The parser is deliberately strict; a table-format change is a test failure by design. It also emits a **collision census** and asserts the two classes separately (§5.5). | [`Features.md`](./Features.md#8-key-bindings-authoritative-reference) is declared the single source of truth for bindings. A test against a hand-copied list makes the *copy* the source of truth and lets the document drift silently. The strictness is the point: a doc edit the code has not absorbed should break the build. | If §8 ever moves to a data file, the test reads that instead — the principle is unchanged |
| **D1-10** | **`Settings` never takes a string key.** `SettingsSchema` declares each key as a `constexpr Key<T>` with type, default, range, group, and policy-lockable flag; `get`/`set` take a `Key<T>` and return/accept `T`. The one dynamic family — `shortcuts/<context>/<action>` — is declared **once as a key family** with a validator over `KeyContext` × `ActionId`, not as an open string API. | "An undeclared key is a compile-time error" is achievable only if there is no way to *name* a key that does not exist. A `get(QString)` overload, however well-intentioned, makes the guarantee decorative. The shortcuts family is genuinely dynamic and gets a typed generator rather than an exception to the rule. | Stage 15, when `PolicyOverlay` layers on top — the schema already carries the lock flag for it |
| **D1-11** | **`WinSingleInstanceBus` claims with a `Local\`-namespace named mutex, hands off over a named pipe, and calls `AllowSetForegroundWindow` before the secondary exits.** | This is *why* `ISingleInstanceBus` is an interface rather than a `QLocalServer` ([`Tech-Stack-Plan.md` §4.3](./Tech-Stack-Plan.md#43-what-qt-gives-us-for-free-versus-what-needs-platform-code)): Windows refuses foreground activation from a process that received no input, so a portable implementation forwards the file and then fails to raise the window — the visible symptom being "I double-clicked and nothing happened". `Local\` rather than `Global\` is a deliberate per-session scope: two users on one machine, or one user across fast-user-switching, each get an instance. | If "single instance per machine" is ever wanted it becomes a settings key, not a code change |
| **D1-12** | **`RAWPAGE_UI_WARNING_GRACE` starts empty, and an entry may be added only with a named reason and an owning issue number.** | The rule is that the grace list may only shrink. A list that starts populated has no baseline to shrink from, and "warnings are errors except over there" becomes permanent on day one of `ui/` existing. | Never |
| **D1-13** | **Crash dumps are symbolized through an archived-binary path built now, not at Stage 15.** `-Wl,--build-id` on every target; the hex build id and the unstripped binaries archived per build; `scripts/symbolize.ps1` takes a minidump plus a build id and drives `addr2line`. | The GCC toolchain emits DWARF, not PDB, so there is no symbol server to lean on — [`Architecture-FileMap.md` §3.1](./Architecture-FileMap.md#31-app--composition-root) says decide this here. A crash-handling path that has never symbolized a real dump is not a crash-handling path, and the first real dump arrives during Stage 2's rendering work, not after packaging. | Stage 15 wires the archive into the release artifacts |
| **D1-14** | **Stage 1 ships `en` and `zh-TW` only. `zh-CN` and `ja` are declared in `i18n/locales.json` with `"shipped": false` and named to Stage 15.** | `i18n-lint`'s "missing key in a shipped locale" rule needs a real, non-vacuous set or it is a gate that cannot fail. Declaring the other two as *not yet shipped* makes their absence **data the gate reads** rather than a case nobody wrote — the difference between a planned gap and a forgotten one. | Stage 15's localization pass flips both flags |
| ~~**D1-15**~~ | ⤵ **Withdrawn 2026-08-23.** *The high-contrast path disables our theming rather than re-implementing it.* Removed with the rest of the accessibility surface — see [`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23). `ThemeManager` now ships light, dark and follow-system only, and no longer consults the OS high-contrast setting. | The row is kept as a breadcrumb so every existing citation of D1-15 still resolves, and so that a later reader can see the behaviour was *removed by decision* rather than never considered. | If accessibility is ever restored, this is the decision to reinstate first — it is one line of code and the largest single accessibility win in the shell |
| **D1-16** | **Every persisted vocabulary this stage creates carries a version from its first write.** `settings.ini` gains a `meta/schemaVersion`; the UI-customization blob and the saved shortcut set carry the same stamp. On load, an **older** version runs an ordered migration chain. A **newer** version is read for what we understand and otherwise **preserved untouched** rather than rewritten. And `ActionId.def` gains an optional `RAWPAGE_ACTION_ALIAS("old.string.id", NewEnumerator)` row, so a renamed id keeps resolving saved shortcuts and QAT entries; **alias rows are appended and never deleted.** | Stage 1 creates three vocabularies that are written to disk and read back by later versions — settings keys, `ActionId` strings, and UI-customization blobs — and without this, the first key whose type changes in Stage 9 and the first id renamed in Stage 7 both destroy a user's customizations silently. A version field costs one line now. The **downgrade** rule is the half people forget: an older build that rewrites a newer build's settings file is data loss during a rollback, which is exactly when a user is least able to tolerate it. This was raised as a candidate open decision against [`Implementation-Plan.md` §8](./Implementation-Plan.md#8-open-decisions-carried-forward) and settled here instead, so it never becomes a cross-stage question nobody owns. | Stage 14 when the preferences surface is complete; Stage 15's `PolicyOverlay` layers on top of the same versioning |

---

## 4. File manifest

Every file Stage 1 creates, with the [`Architecture-FileMap.md`](./Architecture-FileMap.md) row it must
flip **○ → ✅ in the same change**. Rows marked ⚠️ have no FileMap row yet and are lockstep debt — §10.

| File | FileMap row | Task |
|---|---|---|
| `app/main.cpp` (rewritten) | §3.1 | T1 |
| `app/CrashHandler.{h,cpp}` | §3.1 | T1 |
| `app/Application.{h,cpp}` | §3.1 | T8 |
| `app/CompositionRoot.{h,cpp}` | §3.1 | T8 |
| `app/Services.h` | §3.1 | T8 |
| `app/CommandLine.{h,cpp}` | §3.1 (S1 basic) | T8 |
| `app/SingleInstance.{h,cpp}` | §3.1 | T8 |
| `core/cmd/ActionId.def`, `ActionId.h` | §3.3 | T3 |
| `core/cmd/ActionRegistry.{h,cpp}` | §3.3 | T3 |
| `core/cmd/KeyContext.h` | §3.3 | T3 |
| `core/cmd/KeyBindingStore.{h,cpp}` | §3.3 | T4 |
| `core/settings/Settings.{h,cpp}` | §3.8 | T5 |
| `core/settings/SettingsSchema.{h,cpp}` | §3.8 | T5 |
| `core/settings/RecentFiles.{h,cpp}` | §3.8 | T5 |
| `platform/platform.h` | §3.12 | T7 |
| `platform/win/PlatformFactory.{h,cpp}` | §3.12 | T7 |
| `platform/win/WinSingleInstanceBus.{h,cpp}` | §3.12 | T7 |
| `platform/win/WinShellIntegration.{h,cpp}` (assoc only) | §3.12 (S1 assoc) | T7 |
| `ui/shell/MainWindow.{h,cpp}` | §3.13 | T9 |
| `ui/shell/StatusBar.{h,cpp}` | §3.13 | T9 |
| `ui/shell/SidebarDock.{h,cpp}` | §3.13 | T9 |
| `ui/shell/ThemeManager.{h,cpp}` | §3.13 | T9 |
| `ui/shell/Ribbon.{h,cpp}` | §3.13 | T10 |
| `ui/shell/RibbonTabBuilder.{h,cpp}` | §3.13 | T10 |
| `ui/shell/ActionFactory.{h,cpp}`, `RawPageAction.h` | ⚠️ **no row** | T10 |
| `ui/shell/QuickAccessToolbar.{h,cpp}` | §3.13 | T11 |
| `ui/shell/UiCustomization.{h,cpp}` | §3.13 | T11 |
| `ui/shell/ToolFinder.{h,cpp}` | §3.13 | T11 |
| `ui/shell/DocumentTabBar.{h,cpp}` | §3.13 | T12 |
| `ui/shell/DocumentWindow.{h,cpp}` (stub, D1-2) | §3.13 | T12 |
| `ui/shell/ShortcutBinder.{h,cpp}` | §3.13 | T13 |
| `ui/panes/LogPane.{h,cpp}` | §3.15 | T14 |
| `ui/dialogs/PreferencesDialog.{h,cpp}` + General, Shortcuts pages | §3.15 | T14 |
| `tools/i18n-lint/` | §3.17 | T6 |
| `i18n/rawpage_en.ts`, `rawpage_zh_TW.ts`, `locales.json` | ⚠️ **no row** | T6 |
| `res/ribbon.json`, `res/rawpage.qrc`, icons | ⚠️ **no row** | T10 |
| `tests/ui/` (new target, D1-4) | §3.17 | T15 |
| `scripts/symbolize.ps1` | ⚠️ **no row** | T1 |

---

## 5. Component design

Only the parts where a later reader would otherwise have to guess. Anything not described here is
built exactly as [`Architecture-FileMap.md`](./Architecture-FileMap.md) describes it.

### 5.1 `core/cmd/ActionId.def` and `ActionRegistry`

One table, four artifacts (D1-6):

```
RAWPAGE_ACTION(FileOpen,          "file.open",           KeyContext::Global,  "action.file.open")
RAWPAGE_ACTION(FileClose,         "file.close",          KeyContext::Global,  "action.file.close")
RAWPAGE_ACTION(ViewToggleSidebar, "view.toggleSidebar",  KeyContext::Global,  "action.view.toggleSidebar")
…
```

`ActionRegistry` holds, per id: a handler, an enablement predicate, a checked-state predicate, a
label key, an icon name, and a tooltip key. Its whole public surface:

```cpp
Result<void> register_(ActionId, Handler, Enable, LabelKey);   // `register` is a keyword
Result<void> dispatch(ActionId, KeyContext);
bool         isEnabled(ActionId) const;
```

Three properties the tests pin down, because each is a distinct way this layer quietly stops being
real:

- **`dispatch` on an unregistered id returns an `Error` — it does not assert and does not no-op.**
  The batch runner will one day dispatch ids from a file a user wrote; a silent no-op there is a
  batch run that reports success and did nothing.
- **`dispatch` on a registered-but-disabled id returns a *distinct* error code** from the unknown-id
  case. The CLI and the batch report must be able to tell "that does not exist" from "that is not
  available right now".
- **Registration is not idempotent** — a second `register_` for the same id is an error, not an
  overwrite. Two stages both claiming `edit.copy` should fail loudly at startup, not race.

**Stage 1 registers roughly 60–80 of the ~200 ids it declares.** The rest are declared — so the
ribbon can place them and the catalog can name them — and left unregistered, which renders them as
visible, disabled buttons: the honest state. `RibbonTabBuilder` distinguishes the two in the tooltip:
*"arrives in Stage 7"* for unregistered, versus *"not available for this document"* for registered
but disabled.

### 5.2 `core/settings/SettingsSchema` and `Settings`

```cpp
template <class T> struct Key {
    std::string_view path; T defaultValue; Group group; bool policyLockable; /* optional min/max */
};

namespace keys::general {
    inline constexpr Key<Theme>  theme       { "general/theme",       Theme::FollowSystem, Group::General, true };
    inline constexpr Key<double> uiFontScale { "general/uiFontScale", 1.0, Group::General, true, /*min*/0.75, /*max*/2.0 };
    …
}
```

`Settings::get(keys::general::theme)` returns `Theme`. There is no string overload (D1-10), so an
undeclared key is a name that does not exist and the error lands at the call site.

Backing store: `QSettings` in INI format at `%APPDATA%\RawPage\settings.ini` — the one filesystem write
`core/settings/` is permitted, already carved out in `tools/layer-lint/rules.json`. Two behaviours
that matter much later and cost nothing now:

- **A value outside its declared range is clamped and logged, not rejected.** A hand-edited INI or a
  downgraded install must not prevent startup; the log line is what makes it diagnosable.
- **`changed(Key)` fires on an actual change only.** From Stage 2, flow J has `RenderCache`
  invalidating tiles off this signal; a `set` to the same value that invalidates the cache is a
  performance bug that will be blamed on the renderer for a week.

The `shortcuts/<context>/<action>` family is generated over `KeyContext` × `ActionId`, so a shortcut
key for an id that does not exist cannot be written, and a stale key from a removed id is detected
and dropped at load with a log line rather than silently retained.

### 5.3 The A3 spike — criteria fixed before it starts

**Timebox: 3 working days.** If the criteria are not settled by then, that is itself the verdict and
the fallback starts. The failure mode this timebox exists to prevent is a spike that becomes the
implementation without anyone deciding it should.

The spike is a **throwaway** `tools/spike-ribbon/`, deleted at T10 exactly as `tools/spike-pdfium/`
is deleted at Stage 2. Its durable outputs are the verdict and whichever widget techniques survive.
*(**Deleted 2026-08-24 at T10.** All three surviving techniques are now `ui/shell/Ribbon.{h,cpp}`, with
`tests/ui/tst_ribbonbuild.cpp` carrying the checks its `--self-test` used to run.)*

| # | Criterion | How it is judged |
|---|---|---|
| **R-1** | A tab hosts groups containing a large button, a split button, two rows of small buttons, a gallery, and a combo, all aligned to a ribbon's baseline grid | Visual, against a reference (P-4) |
| **R-2** | A group **collapses to a single popup button** when the window narrows past its threshold, and re-expands | The hard part. Resize across the threshold 20 times with no layout thrash |
| **R-3** | Tabs overflow to a chevron when more exist than fit | Visual |
| **R-4** | A contextual tab appears and disappears without the ribbon changing height or the tab strip flickering | Visual, at 60 Hz |
| **R-5** | `Alt` key tips, two levels deep, dismissable with `Esc` | Keyboard-only walk |
| **R-6** | Correct at 100 / 150 / 200 % scaling, **including a per-monitor DPI change while running** | Manual, two monitors — guide case M-02 |
| **R-7** | `F9` collapses to tab-strip-only and restores, preserving the active tab | Keyboard |
| ~~**R-8**~~ | ⤵ **Withdrawn 2026-08-23** — *every control reachable by `Tab`/arrows and reported to the accessibility layer with a name and a role*. Removed with the rest of the accessibility surface ([`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23)), together with its manual case **M-08** and precondition **P-2**. **The `Tab`-reachability half survives as keyboard operability**, which is retained and is asserted mechanically by the spike's focus-order self-test. | ⤵ n/a |
| **R-9** | A tab switch completes within one 60 Hz frame on the reference machine | `tests/bench`, measured, not judged |

**Verdict scale** — recorded in `Tech-Stack-Plan.md` §3.3 beside A5 and A6:

- **Go** — all nine hold with plain `QWidget` composition. `Ribbon` productionizes the spike.
- **Go, with named custom work** — R-1…R-7 hold but a named subset (most likely R-2's collapse
  behaviour) needs a custom `QLayout` or custom painting. **This is still a go**: a custom layout
  inside Qt Widgets is not a fallback, it is ordinary Qt.
- **No-go** — a criterion cannot be met by the widget system at all (realistically only R-6 could
  produce this, now that R-8 is withdrawn). The fallback is a first-party ribbon widget, **still Qt Widgets** and *not* a
  toolkit change, and it costs T10 roughly triple.

Writing the scale down before the spike is what stops "we made something that looks like a ribbon"
from being scored as a go.

### 5.4 `ui/shell/ActionFactory` and the no-direct-connection rule

```cpp
class RawPageAction : public QAction {
public:
    RawPageAction(ActionId, ActionRegistry&, QObject* parent);
    ActionId id() const;
    int      directConnectionCount() const;   // wraps the protected QObject::receivers()
};
```

`ActionFactory` is the **only** place that constructs one, and it connects `triggered` to
`registry.dispatch(id, binder.activeContext())` — one connection, always the same one.

Enforcement (D1-7): `layer-lint` gains a token rule matching `QAction(`, `new QAction`, and
`addAction(` with a string literal, allowed only in `ui/shell/ActionFactory.cpp`, `escapable: false`.
The debug self-check runs from `MainWindow::showEvent`, walks `findChildren<QAction*>()`, and asserts
each is a `RawPageAction` with `directConnectionCount() == 1`.

The guide runs the self-check deliberately against a **planted violation** in `rawpage_ui_tests`,
because a self-check nobody has ever seen fail is a self-check nobody knows works. That is I-15's
lesson stated as a test.

### 5.5 `KeyBindingStore` and the collision census

Seeded from `Features.md` §8, overridable per user, persisted under `shortcuts/`. Lookup is
`(chord, KeyContext) → optional<ActionId>`, resolving **innermost context first, then `Common`, then
`Global`**. That order *is* the model — it is what makes `Ctrl+K` mean Preferences in the Viewer and
Insert-hyperlink in Write mode.

The store emits a **collision census** over the parsed tables, in two classes that must never be
conflated:

- **Class 1 — a context redefines a chord `Global` also claims.** These are what `shadows()` reports,
  because a user remap here can make a Global chord unreachable inside one context.
- **Class 2 — a context overrides a `Common` chord.** §8's Common table says in its own heading that
  contexts may override it, so these are by construction not shadows at all.

**A census of §8 as it stands today gives 6 in class 1 and 9 in class 2:**

| Class | Count | Chords |
|---|---|---|
| 1 — shadows a Global chord | **6** | `Ctrl+D`, `Ctrl+K`, `F11`, `Ctrl+Shift+L`, `Ctrl+Shift+A`, `Ctrl+Shift+G` |
| 2 — overrides a Common chord | **9** | `Ctrl+X`, `Ctrl+C`, `Ctrl+V`, `Ctrl+A`, `Delete`, `Backspace`, `Esc`, `F2`, `Ctrl+F` |

Neither of those is the **"twelve deliberate collisions"** the document set quotes, and chasing that
discrepancy (issue **I-01**, now closed) turned out to be worthwhile: the figure is **correct but was
never scoped**. Twelve counts **Write mode's** collisions specifically — **8** chords overriding
Viewer and **4** overriding Global, 0 overriding Common — and that scope is now stated in
[`Features.md` §8](./Features.md#what-the-twelve-deliberate-collisions-counts), which owns it.

So `tst_keybindings` asserts **three independent numbers**, and conflating them is what made the
figure look wrong in the first place: **12** (the Write-mode set, which Stage 13's exit criteria
depend on), **6** (class 1), and **9** (class 2).

### 5.6 `tools/i18n-lint`

The third gate. Like the other two it links **nothing but the C++ standard library** (D0-1), so
`checks` still runs on the bare Linux `gates` runner with no Qt installed.

| Rule | Fails on |
|---|---|
| `unwrapped-literal` | A string literal in `ui/`, `app/`, or `core/` that reaches a user and is not inside `tr()` / `QT_TR_NOOP` / `Result`'s message-key argument |
| `missing-key` | A key present in `rawpage_en.ts` and absent from any locale marked `"shipped": true` in `i18n/locales.json` |
| `orphan-key` | A catalog key no source file references — **S3, reported not failed**, because a key may legitimately lead its use by a commit |
| `action-label` | An `ActionId` in `ActionId.def` with no `action.*` catalog entry |

`unwrapped-literal` is a **heuristic** and is therefore the one escapable rule, using the same
`// i18n-lint: allow-literal(reason)` shape `layer-lint` already uses for filesystem writes, and the
same `--explain` census. Stage 0's I-20 is the standing warning: the census must count *applied*
waivers only. And I-15's: the fixtures must cover more than one spelling — a raw literal, a
`QStringLiteral`, a `u"…"`, a literal built by concatenation, a literal passed straight to
`setWindowTitle`, and a literal inside a comment as a negative control.

### 5.7 Test targets

| Target | Links | Suites | Runs where |
|---|---|---|---|
| `rawpage_tests` (existing) | `rawpage_core`, `rawpage_pdf` | `tst_actionregistry`, `tst_keybindings`, `tst_settings`, `tst_recentfiles`, `tst_commandline` | Windows + `portable-asan` |
| `rawpage_ui_tests` (new, D1-4) | `rawpage_core`, `rawpage_ui` | `tst_context`, `tst_ribbonbuild`, `tst_qat`, `tst_toolfinder`, `tst_theme`, `tst_actionwiring` | **Windows only**, `QT_QPA_PLATFORM=offscreen` |
| `rawpage_platform_tests` (new, T7) | `rawpage_core`, `rawpage_platform` | `tst_platform` — the interface census, the OS object names, and a real claim/forward/deliver round trip | **Windows only**. Same reasoning as D1-4 from the other side: `tests/unit`'s headless assertion forbids linking `rawpage_platform`, and the first platform test is exactly the pressure that would otherwise weaken it |
| gate fixtures (existing) | — | `i18n-lint` and the `platform/platform.h` cases join the `layer-lint` / `license-audit` ones | Windows + Linux |

**Amended in place, per this file's header.** Two suites arrived that the table above does not
name: **`tst_shell`** at T9, with the first widgets, and **`tst_documenttabs`** at T12. Both were
predictable in hindsight and neither is a deviation from the design — the table was a forecast of
subjects, and the shell frame and the document tabs are two the forecast simply missed.
**`tst_context` arrived at T13**, as forecast and under its forecast name. The live list is
`tests/ui/CMakeLists.txt`.

`tst_commandline` lives in the headless suite even though `CommandLine` sits in `app/`: it is a pure
parse of a `QStringList` into an intent, and the parser header is includable without linking the
executable. If that ever stops being true, the parser has grown a dependency it should not have —
which is itself the finding.

---

## 6. Task breakdown

Ordering is risk-first. **T2 comes before every line of production UI**, because a no-go verdict
triples T10 and reshapes T9's layout assumptions, and learning that after T9 is a rewrite.

| # | Task | Depends on | Content | Done when |
|---|---|---|---|---|
| **T1** | Entry point, DPI awareness, crash handler, `-municode` verdict | — | `main.cpp`: per-monitor-v2 DPI awareness **before any Qt object exists**; `CrashHandler::install()` (`SetUnhandledExceptionFilter` + lazily-loaded `dbghelp` `MiniDumpWriteDump` → `%LOCALAPPDATA%\RawPage\crashes\`); `-Wl,--build-id` on all targets; `scripts/symbolize.ps1`; the D1-3 measurement | A forced crash writes a dump; `symbolize.ps1` resolves the faulting frame to a function name; the `-municode` finding is written down either way |
| **T2** | **A3 ribbon spike — the go/no-go** | T1 | `tools/spike-ribbon/`, criteria R-1…R-9 (R-8 withdrawn 2026-08-23), three days | **A3 verdict recorded** in `Tech-Stack-Plan.md` §3.3 with per-criterion results; R-6 and R-8 handed to the guide's manual cases and marked outstanding until walked |
| **T3** | `ActionId.def`, `ActionRegistry`, `KeyContext` | — | §5.1; ~200 ids declared across the eight `Features.md` sections | `tst_actionregistry` green including the three §5.1 properties |
| **T4** | `KeyBindingStore` + §8 defaults + census | T3 | §5.5; `shadows()`; per-user override and persistence | `tst_keybindings` green; the census computed and reported; **I-01 open until the count is reconciled** |
| **T5** | `SettingsSchema`, `Settings`, `RecentFiles` | — | §5.2; the twelve Stage 1 keys from the plan's *Data / settings keys* list | `tst_settings`, `tst_recentfiles` green; an undeclared key does not compile, proven by a negative-compile fixture |
| **T6** | `tools/i18n-lint` + catalogs + third gate | T3 | §5.6; `i18n/locales.json`; `rawpage_en.ts`, `rawpage_zh_TW.ts`; wired into `RawPageChecks.cmake` | `checks` runs **three** gates and *says* three; every i18n fixture fails with its named diagnostic; both positive controls pass |
| **T7** | `platform/platform.h` + Windows factory | — | All **seventeen** interfaces declared (written here as fifteen; corrected by measurement at T7 — see `Tech-Stack-Plan.md` §4.3), nothing else implemented; `PlatformFactory`; `WinSingleInstanceBus` (D1-11); `WinShellIntegration` file-association only | `platform.h` includes no Win32 header, **enforced by a `layer-lint` layer entry for that one file**; `layer-lint` clean; a second launch forwards and the first raises |
| **T8** | `app/` composition root | T3, T5, T7 | `CompositionRoot`, `Services.h`, `Application`, `CommandLine` (open-with-file + `--lang`), `SingleInstance` | Flow A runs end to end; `MainWindow` receives only interfaces; `tst_commandline` green |
| **T9** | `MainWindow`, `StatusBar`, `SidebarDock`, `ThemeManager` | T8, T2 | The frame; pane hosting with dock / float / auto-hide / width memory; theme tokens and follow-system (D1-15's high-contrast path was withdrawn 2026-08-23) | Window opens; panes dock, float and auto-hide; theme switches live |
| **T10** | `Ribbon`, `RibbonTabBuilder`, `ActionFactory`, `res/ribbon.json` | T2, T3, T9 | Productionize the spike per its verdict; full tab/group skeleton with representative actions; build-time JSON validation (D1-8); `RawPageAction` + the lint rule (D1-7); **spike deleted** | Ribbon builds entirely from JSON; an unknown id fails the build; `tst_ribbonbuild` green |
| **T11** | `QuickAccessToolbar`, `UiCustomization`, `ToolFinder` | T10 | Add / remove / reorder / placement / reset; serialize customizations; fuzzy search over the registry showing each match's chord | A QAT customization survives restart; the tool finder finds and runs a command |
| **T12** | `DocumentTabBar`, stub `DocumentWindow` | T9 | D1-2; tabs, reorder, tear-off to a new window, re-dock, middle-click close, overflow | Tear a tab out and re-dock it; `Ctrl+Tab` and `Ctrl+F6` work |
| **T13** | `ShortcutBinder` + full chord wiring | T4, T10, T12 | `activeContext()` over the focus chain; resolve and dispatch; the debug self-check | Every §8 Global default resolves correctly; `tst_context` green over synthetic chains |
| **T14** | `LogPane`, `PreferencesDialog` (shell + General + Shortcuts) | T5, T11, T13 | Every page built **from `SettingsSchema`**, not hand-laid-out; the Shortcuts page edits `KeyBindingStore` and shows shadows | Changing a setting propagates (flow J); a remap persists and takes effect |
| **T15** | Test target, CI, verdicts, doc lockstep | all | `tests/ui/` (D1-4) into the `windows` CI job; `Architecture-FileMap.md` rows ○ → ✅; `Implementation-Plan.md` Stage 1 status; the A3 verdict; §10's debt | CI green on three jobs at the new counts; every §4 row flipped; every §8 verdict filled in |

---

## 7. Blocking preconditions — setup checklist

Owner: **you**, before T2 can be judged and before the guide's manual half can be walked.

| # | Precondition | Why | State |
|---|---|---|---|
| **P-1** | **Two monitors at different scale factors** (e.g. 100 % and 150 %), at least one able to change scale while the app runs | R-6 and guide case M-02. The mid-session per-monitor DPI change is the one that finds real bugs, and it cannot be faked offscreen | 🟡 **Hardware ready** — two 2560×1440 displays confirmed 2026-08-20. You set the differing scale factors when M-02 is walked |
| ~~**P-2**~~ | ⤵ **Withdrawn 2026-08-23** — *a screen reader (Narrator)*. Removed with R-8 and M-08 when accessibility left the product; see [`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23). | — | ⤵ No longer required |
| **P-3** | Your judgement available for the **zh-TW** review in guide case M-06 | Per your answer of 2026-08-19, Stage 1 ships real Traditional Chinese, reviewed by you rather than by a lint that can only count keys | ✅ **Confirmed 2026-08-20** |
| **P-4** | A reference for what "a real ribbon" means — Right PDF Professional or Office on the same machine | R-1 and R-4 are judged against a reference, and "looks fine" is not a result | ✅ **Confirmed 2026-08-20** |

None of these blocks T1, T3, T5, T6, T7 or T8, so implementation starts regardless. They gate the
**verdict**, not the work.

---

## 8. Verdict ledger — what Stage 1 owes

| Verdict | Produced by | Recorded in |
|---|---|---|
| **A3** — can Qt Widgets host a real ribbon? | T2, plus guide cases M-01 (R-1…R-5, R-7), M-02 (R-6), and the `tests/bench` figure for R-9. *(M-08/R-8 withdrawn 2026-08-23.)* | `Tech-Stack-Plan.md` §3.3, beside A5 and A6; summarized in the Stage 1 status block |
| **`-municode`** — needed or not (D1-3) | T1 | `Implementation-Plan.md` Stage 1 status, as the resolution of Stage 0's deviation (a) |
| ~~**§8 collision census**~~ | ~~T4~~ | ✅ **Settled 2026-08-20 before implementation** — `Features.md` §8 now scopes "twelve" to Write mode (8 over Viewer + 4 over Global). T4 asserts it rather than discovering it |

A stage that is green but has no verdicts is not signed off.

---

## 9. CI changes

| Job | Before | After |
|---|---|---|
| `gates` (ubuntu, no Qt) | 25 cases, two gates | **Three** gates; plus the `i18n-lint` fixture cases and the `platform/platform.h` ones. Still no Qt, still runs before any compile. **At T10 the preset stands at 65 cases locally** (55 at T7; T8 and T9 added the `argv-path-read` rule with its entry-point control and `ui/`'s two permitted interface headers with a control for each; T10 added the `bare-qaction` rule across **four spellings with two controls**), of which the Linux job runs all but the `wrong_compiler` set |
| `windows` (self-hosted) | 66 tests | Plus the `rawpage_tests` additions, plus **`rawpage_platform_tests`** (new at T7, Windows only), plus the new **`rawpage_ui_tests`** target under `QT_QPA_PLATFORM=offscreen`, plus the `tests/bench` R-9 figure |
| `portable-asan` (ubuntu) | 52 tests | Plus the `core/` additions **only**. Neither `rawpage_ui_tests` nor `rawpage_platform_tests` runs here — the portable subset is `core/` + `pdf/cos/` by rule, and adding Qt Widgets or Win32 to a Linux job to chase coverage would quietly redefine what that job proves |

One new CI property worth asserting outright: **`rawpage.exe` must start and exit cleanly under
`offscreen`**, because that is what makes every later stage's headless smoke test possible.

---

## 10. Lockstep debt this stage creates

Owed by **T15**, in the same change as the code:

- `Architecture-FileMap.md` §3.13 has no rows for `ui/shell/ActionFactory` or `RawPageAction.h`; §3.15
  has none for the Shortcuts and General preference pages as separate files. Add them, or fold them
  into an existing row with a ⤵ breadcrumb.
- ~~`i18n/` has no row~~ — ✅ **done 2026-08-23 at T6**: `Architecture-FileMap.md` §3.18 gained a row
  for `i18n/locales.json` and one for the two catalogs together, plus `tools/common/LineScan.h` in
  §3.17. **`tests/platform/` got its row at T7**, in the same change as the target. `res/`,
  `tests/ui/` and `scripts/symbolize.ps1` still have none; `res/` is *data, not code* per
  `CLAUDE.md`, so one row is likely right rather than one per file.
- **New at T7, and already paid:** `Tech-Stack-Plan.md` §4.3 was missing `IPrinting` entirely and
  stated its count in table rows rather than interfaces (issue **I-10**). Both fixed in the same
  change as `platform/platform.h`, since the header is what made the gap visible.
  `platform/PlatformModule.cpp`, Stage 0's placeholder TU, was **deleted** in the same change — its
  own comment said to delete it once the layer had real content, and nothing had called its anchor
  since T1 rewrote `main.cpp`.
- **New at T7, partly paid at T9:** `core/CoreModule.cpp` is still in exactly that state — `core/`
  has had real translation units since T3 and nothing calls `coreModuleAnchor()`; T15's sweep deletes
  it. **`ui/UiModule.cpp` was deleted at T9**, as its own comment instructed, because `ui/shell/` is
  now real content and the archive is no longer empty without it. `pdf/PdfModule.cpp` is **not** dead:
  its layer still has no other TU, and Stage 2 deletes it.

- **New at T8/T9, and already paid:** `Architecture-FileMap.md` gained rows for
  `ui/shell/Catalog.{h,cpp}` + `ShellKeys.def` and for `tests/ui/`, in the same change as the code.
  Two `tools/layer-lint/rules.json` gaps were found by needing them and closed with fixtures and
  controls — see **I-15** and **I-16** in the testing guide's §8.

- **T10 paid three of the four rows this section had been carrying**, in the same change as the code:
  `ui/shell/ActionFactory.{h,cpp}` + `RawPageAction.h` (one row, since neither is meaningful without
  the other), `res/ribbon.json` — **one row for the file rather than one for `res/`**, because the
  directory is data by rule and the ribbon resource is the only thing in it that anyone can get wrong
  — and `tools/ribbon-validate/`. It also added a row for `ui/shell/RibbonModel.{h,cpp}`, which the §4
  manifest does not list at all; see T10's deviation (a) for why that file exists.
  **Still unpaid: `scripts/symbolize.ps1`**, which has had no row since T1.

- ~~**New at T10, and carried rather than paid:** a ribbon narrower than one collapsed group
  **clips**~~ — ✅ **paid at T11.** The ribbon body sits in a `QScrollArea`, so
  `RibbonPageLayout::minimumSize()` went back to the honest content minimum (the sum of the collapsed
  groups) without that number reaching the main window, and the row scrolls instead of being cut off
  at the window edge. Collapse is still exhausted first. Issue **I-19** is fully closed.
- **New at T11/T12, and already paid:** `Architecture-FileMap.md` rows for
  `ui/shell/{QuickAccessToolbar,UiCustomization,ToolFinder,DocumentTabBar,DocumentWindow}` were
  flipped in the same change as the code, and the `MainWindow`, `Ribbon`, `StatusBar`,
  `ActionRegistry`, `Settings` and catalog rows were corrected with them. **A new open decision
  arrived rather than a new debt: O11** (`Implementation-Plan.md` §8), the parameterized-command
  question `ui.qatAdd` raised and Stage 14 owns.
- ~~`CLAUDE.md`'s gate table lists four gates as though all four exist~~ — ✅ **done 2026-08-23**: the
  table carries a *State* column, and the Stage-0-era summary was replaced with a live status block.
  ~~It still says two; T6 must change it to three.~~ ✅ **done 2026-08-23 at T6** — it says three, and
  `checks` says three.
- ~~`README.md`'s *Current state* section~~ — ✅ **done 2026-08-23**, and ✅ **again at T9**: the
  sentence saying the executable opens no window was T9's to remove, and T9 removed it. Both
  `README.md` and `claude-plan/README.md` now describe the window and name what is still missing
  from it.
- `Implementation-Plan.md` §5's guide-table row for Stage 1 flips from ⚪ Queued to ✅ Written **when
  the guide is written**, not at T15 — that one is part of this change, not of T15.

---

## 11. Explicitly out of scope

Planned absences. Filing them as bugs wastes a triage cycle and, worse, invites someone to build them
early — which is how a stage's risk retirement gets scrambled.

| Not in Stage 1 | Arrives at |
|---|---|
| Opening, parsing, or rendering any PDF — `Ctrl+O` opens a **stub tab** (D1-2) | Stage 2 |
| `mutation-lint`, `Command`, `CommandJournal` — `Ctrl+Z` / `Ctrl+Y` are registered and dispatch to an **empty journal** | Stage 3 |
| Contextual ribbon tabs *populated* — the mechanism is proven at R-4, the content is per-feature | Stages 5–13 |
| Per-document pane state and session restore of documents — `SessionState` is Stage 4 | Stage 4 |
| The full Preferences page set — three pages here; 26 groups exist | Stage 14 |
| `WinShellIntegration` beyond file association — thumbnails, preview, index filter, jump list | Stage 15 |
| `PolicyOverlay` — the schema carries the lock flag, nothing reads it yet | Stage 15 |
| `zh-CN` and `ja` catalogs (D1-14) | Stage 15 |
| ~~The full accessibility pass and a screen-reader rig~~ | ⤵ **Withdrawn 2026-08-23** — accessibility is out of the product, not deferred. See [`Features.md` → *Removed from scope*](./Features.md#removed-from-scope--accessibility-2026-08-23) |
| Batch and the CLI beyond open-with-file and `--lang` | Stage 14 |

---

## 12. Risks inside Stage 1

| Risk | Shape | Mitigation |
|---|---|---|
| **A3 comes back no-go** | T10 triples; T9's layout assumptions change | T2 is first, and the §5.3 verdict scale separates "needs a custom layout" (ordinary Qt) from "the widget system cannot" (rare). The fallback is scoped in advance |
| **The stub `DocumentWindow` calcifies** | Stage 2 inherits a shape built for a placeholder | D1-2 names the two things Stage 2 replaces. The stub holds **no** state a real document would own |
| **`i18n-lint`'s literal heuristic is too noisy** | People waive everything and the gate dies | It is the only escapable rule, `--explain` censuses waivers, and §5.6 fixes the fixture spellings up front. If the waiver count exceeds a handful the rule is wrong and gets narrowed — the same judgement `layer-lint`'s write rule already carries |
| **The §8-parsing test is brittle** | A harmless doc edit breaks the build | Deliberate (D1-9); the failure names the row it could not parse. The alternative — a hand-copied table — fails *silently*, which is strictly worse |
| **`offscreen` hides real bugs** | A green `rawpage_ui_tests` over a visually broken ribbon | Precisely why R-1…R-7 are manual. The guide's manual half is the residue offscreen structurally cannot see, per `CLAUDE.md` |
| **~200 `ActionId`s is a naming exercise that churns** | Later stages rename ids, breaking saved shortcuts and QAT customizations | Ids are strings in `ActionId.def`, and both `UiCustomization` and the shortcuts family key off them. **D1-16's alias rows are the migration path** — a renamed id keeps resolving its old string forever. That bounds the damage; it does not remove the incentive to get the names right the first time, since every alias is permanent |

---

## 13. Definition of Done for Stage 1

[`Implementation-Plan.md` §2's Definition of Done](./Implementation-Plan.md#definition-of-done--every-stage-no-exceptions),
made concrete:

1. T1…T15 complete; the [exit criteria](./Implementation-Plan.md#stage-1--app-shell-action-layer-settings-and-single-instance) met.
2. `cmake --build --preset x64-release --target checks` clean — **three** gates now, and `checks` says three.
3. `ctest` green on Windows: `rawpage_tests`, `rawpage_ui_tests`, and every Stage 0 gate case.
4. `portable-asan` green over the `core/` additions.
5. Golden-image and round-trip suites: **not applicable** — they arrive at Stages 2 and 3. Stated so
   their absence is a recorded fact rather than an unnoticed gap.
6. [`Stage1-AppShell-Testing-Guide.md`](./Stage1-AppShell-Testing-Guide.md) — written from this plan
   before the code (it was), its automated half run green by me with **actual** output, its manual
   half walked and signed off **by you**. I do not mark a manual case.
7. No open S1 or S2 in the guide's issue log. **I-01 is open now** and must be resolved before sign-off.
8. **A3 has a recorded verdict**, and so does the `-municode` question.
9. `Architecture-FileMap.md` rows flipped ○ → ✅ and §10's missing rows added, in the same change.
10. `Implementation-Plan.md` Stage 1 status block updated to reality, deviations included.
