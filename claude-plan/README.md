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
| ⑤ | `Stage<N>-<Area>-Plan.md` — **written for [Stage 0](./Stage0-Toolchain-Plan.md), [Stage 1](./Stage1-AppShell-Plan.md) and [Stage 2](./Stage2-Rendering-Plan.md)** | *Optional per stage.* One stage's **task-level execution order** and its **stage-local design choices** — the detail that would swamp ④. Owns no product facts; ④ keeps the objectives, exit criteria, and live status. |
| ⑥ | `Stage<N>-<Area>-Testing-Guide.md` — **written for [Stage 0](./Stage0-Toolchain-Testing-Guide.md), [Stage 1](./Stage1-AppShell-Testing-Guide.md) and [Stage 2](./Stage2-Rendering-Testing-Guide.md)** | How one stage is verified: an **automated** section (exact commands and pass conditions, run by the implementer) and a **manual** section (numbered cases for what cannot run headless, walked by a person). |

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
| What must I set up before work can start? | ⑤ §7 — the precondition checklist (Stage 1's is P-1…P-4; **Stage 2's is P-1…P-4 too, and P-2…P-4 are open**) |
| How do I verify stage N by hand? | ④'s *Testing → Manual* line now; ⑥ once stage N is under way |

## Current state

**Stage 0 is complete** — both halves of its verification green and signed off 2026-08-19, CI green on
three jobs, verdicts **A5** and **A6** recorded in ②.

**Stage 1 is COMPLETE — every Definition-of-Done item closed as of 2026-08-31**, the manual walk
included. All fifteen tasks, all nine walkable manual cases signed off by the user, no open issue.

**Stage 2 is under way — T1 … T9 of 18 are complete**, the most recent three on 2026-09-04. Its
execution plan ([⑤](./Stage2-Rendering-Plan.md) — decisions **D2-1…D2-14**, tasks **T1…T18**) and its
testing guide ([⑥](./Stage2-Rendering-Testing-Guide.md) — **78 automated**, **10 manual** cases) were
both written on 2026-08-31, **before any Stage 2 code**, per ④ §5. It carries the **A2** go/no-go and
owns **O7** and **O10**. Writing them corrected two stage tags in ③ that disagreed with ④ — the
guide's **I-01**.

**Where Stage 2 has got to**: the engine boundary and the pinned PDFium (T1–T2), A2's probe verdict
`go` (T3), the 60-file generated corpus (T4), and then the whole first-party COS reader — lexer,
object model, parser, nine filters, cross-reference engine and repair scanner (T5–T8) — which meets
the **Stage 2 exit criterion** on Windows: every one of the 24 `corpus/malformed/` files yields a
report and never a crash. **T9 opened a document and drew a page.** What is still missing is
everything that puts a rendered tile on screen: `core/render/` policy (T10), the scheduler and cache
(T11), and `ui/viewer/` (T13).

| | |
|---|---|
| Tests | **526/526** on `x64-debug` and `x64-release`, measured 2026-09-04; the bare `gates` preset **90/90** with no Qt. Split: `unit.` 265, `ui.` 124, `gates.` 90, `platform.` 19, `render.` 26, `bench.` 2. The per-task history and every deviation live in ④'s Stage 2 block, which is the authority |
| Gates | `layer-lint` **209 files / 1,320 includes** with **six** filesystem-write waivers, `license-audit` clean at **20 components** and 4 Qt modules, `i18n-lint` **136 files / 2 catalogs / 0 waivers**. **Three live**, not four — `mutation-lint` is Stage 3. `tools/ribbon-validate` is a build step, not a gate |
| Verdicts | **A3 = `go, with named custom work`** and **A2's probe = `go`** (both in ②'s §3.3, per criterion). A2's *product* figure is still owed at T18 — D2-1 measures it twice on purpose |
| Manual | **All nine walkable Stage 1 cases ✅**, every one signed off by the user; the roster and what each found are in Stage 1's ⑥ §6. **Stage 2's ten manual cases have not been walked** — none of them is reachable until there is a viewer to look at |
| Issues | Stage 1: **I-01…I-55, none open** — the full log is in Stage 1's ⑥ §8. Stage 2: **six**, of which **I-02** (the licence gate's closure, half-closed at T7), **I-04** (no rule compares a referenced catalog key against the catalogs), **I-05** (PDFium cannot be called from two threads at once) and **I-06** (JBIG2 is declared and decodes nothing — no permissible library, no corpus fixture) are open. Each stage's ⑥ §8 owns its own |

What exists as code: Stage 0's toolchain, gates and CI; the four `core/` vocabulary headers; the entry
point with per-monitor-v2 DPI and a crash handler whose dumps symbolize end to end; the throwaway A3
ribbon spike (`tools/spike-ribbon/`, **deleted at T10, as planned**); the action layer — `ActionId` (**361** declared
actions), `ActionRegistry`, `Chord`, `KeyBindingStore` (240 bindings seeded from ① §8); the typed
settings store (`core/settings/`, where an undeclared key does not compile); the third gate
(`tools/i18n-lint/`) with the `en` and `zh-TW` catalogs at **561 keys each** (measured 2026-09-04); the OS boundary
(`platform/platform.h`, seventeen interfaces declared, two implemented); the composition root that
wires all of it together (`app/`); the application frame (`ui/shell/`); since T14, the Preferences
surface and the log (`ui/dialogs/`, `ui/panes/`, `core/LogBuffer`, `app/LogFile`); and since T15
`tests/bench/`, which holds the one A3 criterion that is a number rather than a judgement.

**The reader is complete and a page can be drawn; nothing WRITES yet.** `pdf/cos/` holds the whole
first-party COS reader — tokenizer, object model, grammar, nine filters, cross-reference engine and
repair scanner — and `pdf/render/` opens documents and rasterizes tiles through the pinned PDFium.
`core/doc/` holds five vocabulary types and no `Document`; `pdf/ops/` and `ui/viewer/` are still
empty, so **a rendered tile has nowhere to be shown** until T13. Nothing anywhere can emit a PDF
byte: the incremental writer is Stage 3's. Meanwhile **`rawpage.exe`
opens a window with a working ribbon**: 9 tabs, 49 groups and 192 controls, every one of them
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
`%LOCALAPPDATA%`. Most controls are **disabled and say why** — **361** commands are declared and
the **twenty-three** this window can service are registered.
**Stage 2 is in progress — T1–T9 of 18** — and stages 3–16 are `⚪ not started`.

**Accessibility was removed from the product on 2026-08-23**, entirely and not deferred. The single
record is ① → *Removed from scope*; every other document cross-references it.

Live task-by-task status, deviations included, is in
[④'s Stage 1 block](./Implementation-Plan.md#stage-1--app-shell-action-layer-settings-and-single-instance)
and [its Stage 2 block](./Implementation-Plan.md#stage-2--pdf-engine-boundary-and-tiled-rendering).

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
