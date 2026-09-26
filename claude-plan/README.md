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
| ⑤ | `Stage<N>-<Area>-Plan.md` — **written for [Stage 0](./Stage0-Toolchain-Plan.md), [Stage 1](./Stage1-AppShell-Plan.md), [Stage 2](./Stage2-Rendering-Plan.md), [Stage 3](./Stage3-Mutation-Plan.md) and [Stage 4](./Stage4-ViewingSurface-Plan.md)** | *Optional per stage.* One stage's **task-level execution order** and its **stage-local design choices** — the detail that would swamp ④. Owns no product facts; ④ keeps the objectives, exit criteria, and live status. |
| ⑥ | `Stage<N>-<Area>-Testing-Guide.md` — **written for [Stage 0](./Stage0-Toolchain-Testing-Guide.md), [Stage 1](./Stage1-AppShell-Testing-Guide.md), [Stage 2](./Stage2-Rendering-Testing-Guide.md), [Stage 3](./Stage3-Mutation-Testing-Guide.md) and [Stage 4](./Stage4-ViewingSurface-Testing-Guide.md)** | How one stage is verified: an **automated** section (exact commands and pass conditions, run by the implementer) and a **manual** section (numbered cases for what cannot run headless, walked by a person). |

**⑤ and ⑥ are forms, not a full set of existing files.** One guide per stage, written **immediately
after that stage's plan and before its first line of code** — the sequence is
**④'s stage entry → ⑤ (where a stage warrants one) → ⑥ → implement → run the guide.** Writing the
guide from the plan keeps verification independent of the implementation; written afterwards it only
confirms whatever got built, including the parts built wrong. The schedule is
[the roadmap's §5](./Implementation-Plan.md#5-per-stage-testing-guides), and both of ⑥'s sections
passing is part of that stage's Definition of Done.

**Stages 0 to 4 have both** — Stage 4's pair written on 2026-09-18, the day Stage 3 was signed off and
before any Stage 4 code. Every later stage has neither yet, and that absence is the correct
state rather than a gap — those files are written when their stage comes up, not in advance.

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
| What must I set up before work can start? | ⑤ §7 — the precondition checklist (Stage 1's is P-1…P-4; Stage 2's is P-1…P-4 too, with P-2 withdrawn and none open; Stage 3's is P-1…P-5, with P-4 replaced by automation; **Stage 4's is P-1…P-4, all cleared — its P-2, a way to remove a monitor while the product runs, was answered on 2026-09-19 as `Win`+`P` → *PC screen only***) |
| How do I verify stage N by hand? | ④'s *Testing → Manual* line now; ⑥ once stage N is under way |

## Current state

**This section deliberately carries no counts.** It once quoted test totals, gate figures, issue
tallies and case states, and every one of them went stale while the documents that own them moved on —
two of its own paragraphs ended up disagreeing about how many commands were registered. So it says
where the work is and points at the owner; the numbers live there and are re-measured there.

| Stage | State | Where the detail is |
|---|---|---|
| 0 — Toolchain | ✅ complete, signed off 2026-08-19 | [⑥ Stage 0](./Stage0-Toolchain-Testing-Guide.md) |
| 1 — App shell | ✅ complete, signed off 2026-08-31 | [④'s Stage 1 block](./Implementation-Plan.md#stage-1--app-shell-action-layer-settings-and-single-instance), [⑥ Stage 1](./Stage1-AppShell-Testing-Guide.md) |
| 2 — Engine boundary and tiled rendering | ✅ complete, signed off 2026-09-14 | [④'s Stage 2 block](./Implementation-Plan.md#stage-2--pdf-engine-boundary-and-tiled-rendering), [⑥ Stage 2](./Stage2-Rendering-Testing-Guide.md) — §6 carries each manual case's ✅ or ☐, §8 the issue log |
| 3 — Document model, command journal, incremental save | ✅ complete, signed off 2026-09-18 | [⑤ Stage 3](./Stage3-Mutation-Plan.md), [⑥ Stage 3](./Stage3-Mutation-Testing-Guide.md), [④'s Stage 3 block](./Implementation-Plan.md#stage-3--document-model-command-journal-incremental-save) |
| 4 — Viewing and navigation surface | 🟡 **CODE COMPLETE — T1 to T4 complete 2026-09-19, T5 2026-09-20, T6 to T11 2026-09-21, T12 to T14 2026-09-23; all fourteen tasks done, awaiting the manual walk** (the corpus: 70 files, 65 golden cases; **A2 at Stage 4: `go`** — measured twice as D4-16 required, T2's bench before any pane existed and **T14's product through the shipped panes, 7.03–8.56 ms against a 16.667 ms budget**, the plain verdict unlocked by the two-window column falling from 18.91–19.33 to 4.01–4.75 ms; `IAcceleratedSurface` answered *not built*; `SessionState`, the three `core/doc/` read-only models, **all three document panes**, `ToolController` with four tools, the rulers, grid, guides and snap, the split, Full Screen and presentation, the window arranger, **Fit Visible over a real content box** with Document Properties, reading mode and logical page labels, **session capture and restore through the product's own composition**, and T14's re-measurement, censuses and lockstep sweep), plan and guide written 2026-09-18 before any code; **all 86 automated cases carry a result** — A-80 was added 2026-09-24 with I-10's fix, A-81 to A-85 on 2026-09-26 with I-11's to I-14's fixes and decision D4-18 (A-85 from a `windows`-platform run: under `ctest` it skips), and A-04, a `portable-asan` job this machine cannot run, was read from Linux CI's log — and **four manual cases are walked — M-11, 2026-09-24, passed; M-02 and M-06, 2026-09-26, passed; M-01, 2026-09-26, failed its first walk and passed its re-walk on the fixed build the same day — and the other eight are what the stage still owes**; **one issue open, I-07 (S4)** — I-10 (S2), M-11's finding, I-11 (S2) and I-12 (S3), M-01's, and I-13 (S3), the vertical ruler's missing numbers, and I-14 (S2), M-02's moved presentation keeping its physical size, were each closed the day they were raised — the other eight closed — **I-05 at T12, which also closed Stage 2's I-28, and I-01 and I-09 at T13**; all four preconditions cleared | [⑤ Stage 4](./Stage4-ViewingSurface-Plan.md), [⑥ Stage 4](./Stage4-ViewingSurface-Testing-Guide.md), [④'s Stage 4 entry](./Implementation-Plan.md#stage-4--viewing-and-navigation-surface) |
| 5 – 16 | ⚪ not started | [④ §3–§4](./Implementation-Plan.md) |

**What is live, in one sentence each, with the owner of every figure:**

- **The product reads, draws, edits one thing and saves it.** `rawpage.exe` opens PDFs into tabs,
  renders them through the pinned PDFium behind our own facade with a first-party COS reader beneath,
  rotates a page through a journalled command, saves incrementally through our own writer, and offers
  what was unsaved after a crash. What each file does is ③; how far each stage got is ④.
- **Verdicts** A2, A3, A4, A5 and A6 are recorded in ② §3.3, each with its measurement and its bound.
  **A2 is re-measured at Stage 4** for that stage's workload, which also decides
  `platform::IAcceleratedSurface`.
- **Test counts, gate figures and waivers** are quoted, re-measured, in [`CLAUDE.md`](../CLAUDE.md) →
  *Where the work is* and *Commands*, and per stage in ⑥.
- **Accessibility was removed from the product on 2026-08-23**, entirely and not deferred. The single
  record is ① → *Removed from scope*; every other document cross-references it.
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
- **Not sure which document a fact belongs in?** The inverse of the ownership table — *I have just
  learned, decided or measured X, where does it get written?* — is
  [`CLAUDE.md` → *Where does this fact go?*](../CLAUDE.md), one row per kind of fact, with the
  house rules for writing it beneath. This index owns no facts, that file owns the routing, and
  neither restates the other.
