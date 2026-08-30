# RawPage — Design Document Set

Upfront design for **RawPage**, a Windows-first desktop PDF editor modeled on
[Right PDF Professional](https://www.rightpdf.com/zh-TW/helpcontent/Right-PDF-Pro/20933-About-this-User-Manual).
Produced with the [`design-pipeline`](./skills-specs-generation/SKILL.md) skill set, in dependency
order.

> **This file is an index only.** It owns no facts. Every statement about the product lives in
> exactly one of the documents below, and the others cross-reference it rather than restating it.

## Read in this order

| # | Document | Owns (single source of truth for…) |
|---|---|---|
| ① | **[Features.md](./Features.md)** | The complete capability catalog, and the **authoritative key-binding tables**. No stack choice, no scope cut. |
| ② | **[Tech-Stack-Plan.md](./Tech-Stack-Plan.md)** | The locked stack and license posture, the directory layout and **inward-only dependency rule**, the **mutation-through-commands** cross-cutting design, and the risk-reducing first milestone. |
| ③ | **[Architecture-FileMap.md](./Architecture-FileMap.md)** | What **every file** does and what it interacts with, plus the primary call-trace flows. |
| ④ | **[Implementation-Plan.md](./Implementation-Plan.md)** | **Stage ordering and live status**, the shared conventions, the testing strategy and Definition of Done, milestones, and traceability. |
| ⑤ | `Stage<N>-<Area>-Plan.md` — **written for [Stage 0](./Stage0-Toolchain-Plan.md) and [Stage 1](./Stage1-AppShell-Plan.md)** | *Optional per stage.* One stage's **task-level execution order** and its **stage-local design choices** — the detail that would swamp ④. Owns no product facts; ④ keeps the objectives, exit criteria, and live status. |
| ⑥ | `Stage<N>-<Area>-Testing-Guide.md` — **written for [Stage 0](./Stage0-Toolchain-Testing-Guide.md) and [Stage 1](./Stage1-AppShell-Testing-Guide.md)** | How one stage is verified: an **automated** section (exact commands and pass conditions, run by the implementer) and a **manual** section (numbered cases for what cannot run headless, walked by a person). |

**⑤ and ⑥ are forms, not a full set of existing files.** One guide per stage, written **immediately
after that stage's plan and before its first line of code** — the sequence is
**④'s stage entry → ⑤ (where a stage warrants one) → ⑥ → implement → run the guide.** Writing the
guide from the plan keeps verification independent of the implementation; written afterwards it only
confirms whatever got built, including the parts built wrong. The schedule is
[the roadmap's §5](./Implementation-Plan.md#5-per-stage-testing-guides), and both of ⑥'s sections
passing is part of that stage's Definition of Done.

**Stages 0 and 1 have both.** Every later stage has neither yet, and that absence is the correct state
rather than a gap — those files are written when their stage comes up, not in advance.

## Where to look for a given question

| Question | Document |
|---|---|
| Does the product do X? | ① |
| What does `Ctrl+K` do, and why does it mean two things? | ① §8 |
| Why PDFium and not MuPDF? | ② §3.2 |
| What am I allowed to `#include` from here? | ② §4.2, restated file-specifically in ③ §1 |
| How does a change to a document actually reach the disk? | ② §5, traced in ③ §5 flow D |
| What does this file do? | ③ §3 |
| What are we building next, and when is it done? | ④ §3–§4 |
| Which stage implements catalog section §N? | ④ §7 |
| What is still undecided? | ② §10 (risks) and ④ §8 (open decisions) |
| What are the concrete tasks for the stage we are on? | ⑤ where it exists; ④'s *Components* otherwise |
| What must I set up before work can start? | ⑤ §7 — the precondition checklist (Stage 1's is P-1…P-4) |
| How do I verify stage N by hand? | ④'s *Testing → Manual* line now; ⑥ once stage N is under way |

## Current state

**Stage 0 is complete** — both halves of its verification green and signed off 2026-08-19, CI green on
three jobs, verdicts **A5** and **A6** recorded in ②.

**Stage 1's code is complete: all fifteen tasks done (2026-08-25), and every automated case is green
in CI.** What remains is the manual walk in ⑥.

| | |
|---|---|
| Tests | **343/343** on `x64-debug` and `x64-release` (334 at T15; +5 from the M-03 fixes, +2 from the M-04 fixes, +2 from M-05's); the bare `gates` preset **66/66** with no Qt. **CI green on all four jobs** (run 32785493072), and the `windows` job **ran for the first time in the project's history**: 334/334 there, `gates` 52/52, `portable-asan` 196/196; plus one benchmark, whose exit code is about whether the measurement was possible rather than whether the machine was fast |
| Gates | `layer-lint` 141 files / 940 includes, `license-audit` clean, `i18n-lint` 78 files / 2 catalogs / 0 waivers. **Three live**, not four — `mutation-lint` is Stage 3. `tools/ribbon-validate` is a build step, deliberately not a fourth gate |
| Verdicts | **A3 = `go, with named custom work`** (②'s §3.3, per criterion). T15 re-measured **R-9 against the shipped 192-control ribbon** rather than T2's spike: p95 **0.471 ms** against a 16.667 ms budget · **`-municode` = not adopted** |
| Manual | **M-01 ✅, M-02 ✅, M-03 ✅, M-04 ✅, M-05 ✅** signed off by the user — M-02 re-walked in full 2026-08-30 with no findings, M-03 walked 2026-08-30 and it found **I-33, I-34 and I-35**, all fixed. **M-04 ✅ signed off 2026-08-30** after three passes and four findings (I-36, I-37, I-38, I-39), each repair making the next defect reachable. **M-05 ✅ signed off 2026-08-31** after I-40 and I-41 — the second found because the first fix changed nothing on screen. **M-11 and M-12 are walkable and not yet walked**; the rest are not walkable |
| Issues | I-01…I-41 in ⑥. **None open.** **I-41 (S2)** the `windows11` style paints control text from its own OS-derived scheme and ignores the app palette, so dark mode reached the labels and not one button; `apply()` now sets the colour scheme too. **I-40 (S3)** dark mode's disabled text — most of this shell's text — was unreadable at 3.05, and light measured 2.54 after two passes had signed it off; both raised, with a contrast case in both themes. **I-39 (S3)** the auto-hide strip stayed up beside the pane it had just revealed — a T9 defect nothing could see until I-37 made peeking performable. **I-37 (S3)** an auto-hidden pane answered a click but not a hover — the user chose hover and the product moved; **I-38** `general/touchMode` is read by nothing and is now a recorded planned absence for Stage 14 rather than a defect. **I-36 (S2)** came from opening M-04: the ribbon's `QTabBar` was laid out **zero pixels wide** from T10 on, so no tab was ever drawn — eleven green ribbon cases had asked the bar what it contained and none what the layout gave it. The last three came from the M-03 walk and were the first in the *product*: **I-34 (S2)**, re-dock could not be performed at all; **I-33 (S3)**, tearing the only tab out of a window left it empty; and **I-35 (S3)**, a window emptied by a re-dock stayed open — all fixed 2026-08-30. T15's three were the harness, its infrastructure, and the guide itself: a benchmark whose every printed figure was its own argument-evaluation bug, and an S2 — a CI job that had never run because two systems had to agree on a variable name and nothing checked that they did |

What exists as code: Stage 0's toolchain, gates and CI; the four `core/` vocabulary headers; the entry
point with per-monitor-v2 DPI and a crash handler whose dumps symbolize end to end; the throwaway A3
ribbon spike (`tools/spike-ribbon/`, **deleted at T10, as planned**); the action layer — `ActionId` (358 declared
actions), `ActionRegistry`, `Chord`, `KeyBindingStore` (240 bindings seeded from ① §8); the typed
settings store (`core/settings/`, where an undeclared key does not compile); the third gate
(`tools/i18n-lint/`) with the `en` and `zh-TW` catalogs at **525 keys each**; the OS boundary
(`platform/platform.h`, seventeen interfaces declared, two implemented); the composition root that
wires all of it together (`app/`); the application frame (`ui/shell/`); since T14, the Preferences
surface and the log (`ui/dialogs/`, `ui/panes/`, `core/LogBuffer`, `app/LogFile`); and since T15
`tests/bench/`, which holds the one A3 criterion that is a number rather than a judgement.

**No document code exists** — nothing in `core/doc/`, `pdf/` or `ui/viewer/` — but **`rawpage.exe`
now opens a window with a working ribbon**: 9 tabs, 49 groups and 192 controls, every one of them
built from `res/ribbon.json` and dispatching through `ActionRegistry`, above a sidebar, a status bar
and a live theme. T11 added a **customizable Quick Access Toolbar** whose every operation is itself a
command, and a **tool finder** whose ranking is a widget-free function so its result order can be
asserted without a window; T12 added **document tabs** over a stub `DocumentWindow` that holds a path
and nothing else (decision D1-2), with reorder, middle-click close, tear-off to a new window and
re-dock. **T13 added the keyboard** — one `ShortcutBinder` per window, resolving innermost context then
`Common` then `Global`, with no chord ever becoming a `QAction` shortcut. **T14 added Preferences and
the log**: three pages whose every row is generated from `SettingsKeys.def` (so a preference added in a
later stage is a table row and no widget code), a Shortcuts page that writes a remap to both the
binding store and the settings file, and a log pane over an in-memory buffer and a rolling file under
`%LOCALAPPDATA%`. Most controls are **disabled and say why** — Stage 1 declares 358 commands and
registers the **nineteen** this window can service, and no PDF is read until Stage 2.
Stages 2–16 are `⚪ not started`.

**Accessibility was removed from the product on 2026-08-23**, entirely and not deferred. The single
record is ① → *Removed from scope*; every other document cross-references it.

Live task-by-task status, deviations included, is in
[④'s Stage 1 block](./Implementation-Plan.md#stage-1--app-shell-action-layer-settings-and-single-instance).

## Keeping the set in lockstep

- ③ and ④ carry **live status** and must track the code. A file's ③ row is part of that file's
  definition of done; a stage's ④ status block is edited continuously, deviations included.
- **Cross-reference, never re-decide.** If the roadmap seems to need a new key binding, fix ①.
- A ④ *Components* entry must resolve to a ③ file row; a ③ flow must use ① features.
- **⑤ and ⑥ own no product facts.** A per-stage plan owns only that stage's task order and its
  stage-local choices; a testing guide owns only how that stage is verified. Both cross-reference
  ①–④ and are corrected in the same change as any design shift, like everything else here. A ⑤ that
  names a file still owes that file a ③ row.
- When a planned file is folded into another, its ③ row stays as a ⤵ breadcrumb rather than being
  deleted.
