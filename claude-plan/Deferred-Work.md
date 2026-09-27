# RawPage — Deferred Work Register

**Work that a stage deliberately did not do, and that no later stage's plan has yet taken in.** Created
2026-09-27, when `Stage4-ViewingSurface-Testing-Guide.md` §8's **I-07** was found to name Stage 8 as its
owner while Stage 8's plan in [`Implementation-Plan.md`](./Implementation-Plan.md) §4 said nothing about
it. A deferral that only the deferring stage records is a sentence nobody downstream reads
(`CLAUDE.md` → *How to write it*, rule 4) — this file is where such an item waits, visibly, until a stage
takes it.

## What this file owns, and what it does not

| Owns | Does not own |
|---|---|
| Each deferred item's **state of ownership**: *unassigned*, or *taken by Stage N* with where | **What the capability is** — `Features.md`, which keeps the line in scope |
| **What exists today** and **what is absent**, as facts checked on a named date | **The defect or finding itself** — the raising stage's guide §8 (`I-nn`) keeps the mechanism and the history |
| Why it was deferred, in one paragraph | **How it will be built** — a repair written by someone who did not build it is a guess (rule 4); the stage that takes it decides |
| | **Anything removed from scope** — `Features.md` → *Removed from scope* is that record |

## The rules

1. **An entry is added when a stage defers work and no later stage's plan names it.** A deferral that
   a later stage's plan *already* lists does not belong here — that plan is its owner.
2. **An entry leaves *unassigned* only when a stage's plan takes it in** — its §4 block in
   `Implementation-Plan.md` or its `Stage<N>-*-Plan.md` — and the entry then records **which stage and
   where**. When that stage builds it, the entry records the commit or the task, and is marked done.
3. **Rows are never deleted.** A finished or withdrawn entry keeps its row with its outcome, as a
   `⤵` breadcrumb does in `Architecture-FileMap.md`.
4. **Labels are `DW-n`**, numbered in order and never reused. Cite one as *`DW-1` (`Deferred-Work.md`)*
   on first use, per `CLAUDE.md` → *Citing a label*.
5. **Re-check the *What exists* column before relying on it** — it is dated because code moves.

## Register

| # | Item | Raised by | State | Owner |
|---|---|---|---|---|
| **DW-1** | **Substitute page colours** — the second half of `Features.md` §1.2's *"invert / substitute page colors for high-contrast reading"* | `Stage4-ViewingSurface-Testing-Guide.md` §8 **I-07** (S4), Stage 4 T8, 2026-09-21 | ⚪ **Deferred** by the user, 2026-09-27 | **Unassigned** — no stage's plan names it |

---

### DW-1 · Substitute page colours

**The capability.** `Features.md` §1.2, *View transforms*: *"Invert / substitute page colors for
high-contrast reading"* and *"Set a custom page background color"*. The **invert** half is built; the
**substitute** half — a chosen page colour and text colour in place of the document's — is not.

**Why it was deferred.** Substitution needs a colour pair that persists, and at Stage 4 T8 no declared
settings key held one; minting one then would have been a key no plan asked for, chosen by the task
that happened to notice (I-07's write-up). I-07 named **Stage 8's colour work** as the owner. **Checked
2026-09-27, that was not true**: Stage 8's plan (*Save, export, and optimize*) carries export colour
space only, `Implementation-Plan.md` §7 maps `Features.md` §1.2 to Stages 2 and 4 alone, and Stage 14's
*"complete Preferences set"* surfaces declared keys rather than declaring this one. Offered three ways
to settle it — build it inside Stage 4, assign it to a later stage's plan, or remove it from scope —
**the user chose to defer it** and record it here.

**What exists today** (checked 2026-09-27, commit `6c4a9b6`):

- `core/cmd/ActionId.def` declares **`view.pageBackground`** (`ViewPageBackground`, context `Viewer`,
  catalog key `action.view.pageBackground`); **it is registered by nothing**, and
  `ui/shell/MainWindow.cpp` says why at the `view.invertColors` registration.
- **Two cases assert that absence**: `tests/ui/tst_rulers.cpp` (guide case **A-52**, recorded 🟡 for
  this half) and `tests/ui/tst_shell.cpp`. Either fails the day the id is registered, which is
  deliberate — whoever takes this item updates them in the same change.
- **`view.invertColors`** is registered: a per-VIEW toggle with no chord and no settings key, painted
  in `ui/viewer/PageView::paintContent` over the tiles the scheduler returned, one page sheet at a time,
  with the grid and guides drawn over it — a paint, not a render flag (D4-11,
  `Stage4-ViewingSurface-Plan.md` §3). Toggling it re-renders nothing.
- **Precedent for a stored colour**: `core/settings/SettingsKeys.def` holds `units/gridColour` as a
  `QString` (`"#6699CC"`), parsed in `ui/viewer/GridGuideLayer`, because `core/` may not name
  `QColor` (convention 3).

**What is absent:** a settings key (or pair) holding a page colour and a text colour; any Preferences
control for them; a registration for `view.pageBackground`; and a paint of substituted colours. No
design decision exists for whether substitution is per view like the inversion or persistent like
the grid colour, or how it combines with the inversion — the stage that takes this decides.

**Taken by:** —
