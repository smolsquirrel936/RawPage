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
| ⑤ | `Stage<N>-<Area>-Plan.md` — **written for [Stage 0](./Stage0-Toolchain-Plan.md), [Stage 1](./Stage1-AppShell-Plan.md), [Stage 2](./Stage2-Rendering-Plan.md), [Stage 3](./Stage3-Mutation-Plan.md), [Stage 4](./Stage4-ViewingSurface-Plan.md), [Stage 5](./Stage5-TextSearch-Plan.md) and [Stage 6](./Stage6-Annotations-Plan.md)** | *Optional per stage.* One stage's **task-level execution order** and its **stage-local design choices** — the detail that would swamp ④. Owns no product facts; ④ keeps the objectives, exit criteria, and live status. |
| ⑥ | `Stage<N>-<Area>-Testing-Guide.md` — **written for [Stage 0](./Stage0-Toolchain-Testing-Guide.md), [Stage 1](./Stage1-AppShell-Testing-Guide.md), [Stage 2](./Stage2-Rendering-Testing-Guide.md), [Stage 3](./Stage3-Mutation-Testing-Guide.md), [Stage 4](./Stage4-ViewingSurface-Testing-Guide.md), [Stage 5](./Stage5-TextSearch-Testing-Guide.md) and [Stage 6](./Stage6-Annotations-Testing-Guide.md)** | How one stage is verified: an **automated** section (exact commands and pass conditions, run by the implementer) and a **manual** section (numbered cases for what cannot run headless, walked by a person). |
| ⑦ | **[Deferred-Work.md](./Deferred-Work.md)** | Work a stage deliberately did not do and that **no later stage's plan has taken in** (`DW-n`) — each item's ownership state, what exists today and what is absent. Not the capability (①) and not the finding (the raising ⑥'s §8). |

**⑤ and ⑥ are forms, not a full set of existing files.** One guide per stage, written **immediately
after that stage's plan and before its first line of code** — the sequence is
**④'s stage entry → ⑤ (where a stage warrants one) → ⑥ → implement → run the guide.** Writing the
guide from the plan keeps verification independent of the implementation; written afterwards it only
confirms whatever got built, including the parts built wrong. The schedule is
[the roadmap's §5](./Implementation-Plan.md#5-per-stage-testing-guides), and both of ⑥'s sections
passing is part of that stage's Definition of Done.

**Stages 0 to 4 have both** — Stage 4's pair written on 2026-09-18, the day Stage 3 was signed off and
before any Stage 4 code — **and Stage 5's pair** (2026-09-29, before any Stage 5 code, its plan's §0
answered the same day), **and Stage 6's pair** (2026-10-04, before any Stage 6 code, its plan's §0
answered the same day). Every later stage has neither yet, and that absence is the correct
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
| What was deferred that no stage has picked up? | ⑦ |
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
| 4 — Viewing and navigation surface | ✅ complete, signed off 2026-09-27 | [⑤ Stage 4](./Stage4-ViewingSurface-Plan.md), [⑥ Stage 4](./Stage4-ViewingSurface-Testing-Guide.md), [④'s Stage 4 entry](./Implementation-Plan.md#stage-4--viewing-and-navigation-surface) |
| 5 — Text layer: extraction, selection, find, search, comparison | ✅ complete, signed off 2026-10-04 — plan and guide written 2026-09-29, §0 answered; **T1 done 2026-09-29, T2, T3 and T4 2026-09-30, T5, T6, T7, T8, T9 and T10 2026-10-01, T11, T12, T13 and T14 2026-10-02** (A2 at Stage 5: `go, with named custom work` at T3, re-graded `go` at T14; the layout baseline meets every target; selection and copy, CF_HTML measured through Win32; the text index and search, the second search of a 900-page file instant at p95 13.2 ms; the find bar; comments, form fields, bookmarks and attachments searched only with their switches on; Advanced Search over a folder, the UI thread opening nothing -- its bench column, outside the budget on T9's day, inside it on T14's; the Snapshot, the region re-rendered at `pageDisplay/snapshotDpi` and equal to a fresh render of the page, none of the overlays in it; Reflow view, `columns-two.pdf` read as the generator's 16 blocks in order and the place kept both ways; `DocumentComparer`, every labelled change of `corpus/compare/` found with its kind and nothing else at text only; the comparison surface, side by side by aligned page, the overlay in two tints, `Ctrl+]` and `Ctrl+[` through both views, and neither document changed; T14, A2 re-read through the shipped surfaces with every column inside the budget -- A2 at Stage 5 re-graded `go` -- and two windows that widened themselves fixed, the find bar's and the comparison's, guide I-10 and I-11), every automated case ticked (CI run 36998382841 on the T14 tree, green); the user's requests during the walk added A-115 … A-129, each ticked; **all nine manual cases walked and passed, M-09 the last on 2026-10-04, and the stage signed off by the user that day** | [⑤ Stage 5](./Stage5-TextSearch-Plan.md), [⑥ Stage 5](./Stage5-TextSearch-Testing-Guide.md) |
| 6 — Annotations and review | 🟡 in progress — plan and guide written 2026-10-04, before any code, **§0 answered the same day** (Q7 moved the PDF-page stamp to Stage 7); **T1 (the corpus), T2 (the model and its first-party reading), T3 (the write path, Highlight end to end), T4 (how an edit reaches the pixels; *A2 at Stage 6* `go, with named custom work`) T5 (the overlay, the context chain, the properties bar) and T6 (text markup, notes and Identity) done 2026-10-05, T7 (free text, callout, typewriter) done 2026-10-07, T8 (drawing, ink, the eraser, the tool defaults and the Commenting page; 31 appearance baselines; PDFium's generation as a control) and T9 (the Comments pane: group, sort, filter, search, replies, statuses, checkmarks, numbers; comments searched from the store) done 2026-10-08, T10 (stamps: standard, dynamic and image, the palette, the custom-stamp library and dialog, a turn from the properties bar, an image pasted) done 2026-10-09, T11 (attachments: the Attachments pane, add, delete, extract and open -- an attached PDF from its bytes, an executable refused -- a pushpin's file, and the recovery journal's side files) done 2026-10-12; next: T12** | [⑤ Stage 6](./Stage6-Annotations-Plan.md), [⑥ Stage 6](./Stage6-Annotations-Testing-Guide.md), [④'s Stage 6 entry](./Implementation-Plan.md#stage-6--annotations-and-review) |
| 7 – 16 | ⚪ not started | [④ §3–§4](./Implementation-Plan.md) |

**What is live, in one sentence each, with the owner of every figure:**

- **The product reads, draws, reads text, annotates and saves.** `rawpage.exe` opens PDFs into tabs,
  renders them through the pinned PDFium behind our own facade with a first-party COS reader beneath,
  navigates and lays them out (Stage 4), selects, finds, searches, reflows and compares their text
  (Stage 5), makes and manages annotations through journalled commands (Stage 6, so far), saves
  incrementally through our own writer, and offers what was unsaved after a crash. What each file does
  is ③; how far each stage got is ④.
- **Verdicts** A2, A3, A4, A5 and A6 are recorded in ② §3.3, each with its measurement and its bound.
  **A2 is re-measured at each stage that adds engine work** -- Stage 4's measurement decided
  `platform::IAcceleratedSurface`: not built, and Stages 5 and 6 each carry their own row (② §3.3).
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
