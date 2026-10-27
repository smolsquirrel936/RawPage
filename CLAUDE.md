# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**This file is a ROUTING document.** It holds what binds every session — the locked stack, the
architecture, the machine-enforced rules, the commands — and points at the document that owns
everything else. It deliberately does **not** carry per-task history, per-file descriptions or live
status; those have owners, listed in the document-set table below, and a second copy here is how a
number goes stale. When something changes, **fix the owning document**; never re-decide a fact here.

## What this repository is

**RawPage** — a Windows-first desktop PDF editor modeled on Right PDF Professional. The design lives
under `claude-plan/`. **Stages 0 to 5 are complete and signed off** (Stage 5 on 2026-10-04) and
**Stage 6, annotations and review, is in progress.** What the built product lets a person do — and
what it deliberately does not, so an absence is not reported as a defect — is
[`claude-plan/Shipped-Surface.md`](claude-plan/Shipped-Surface.md); read it when a task needs that
overview, not by default.

### Where the work is right now — read this first

This table is the entry point a new session resumes from, and it holds the **current** state only.
**Each row is replaced when it changes, never appended to** (rule 11 below): the figure it supersedes
already lives in its owner — the task's row in `Implementation-Plan.md` §4, the guide's status line.

| | |
|---|---|
| **Stage** | **Stage 6 (annotations and review): T1 … T15 done, its code complete**, T15 (the re-measurement, the census, CI and the lockstep) on 2026-10-20; **not signed off** -- its manual half is unwalked. Each task's detail, deviations, issues found and CI runs are its row in `Implementation-Plan.md`'s Stage 6 block; its cases and issue log are `Stage6-Annotations-Testing-Guide.md` §5–§8, its sign-off checklist §9. Stages 0–5 signed off; the one open issue carried from them is Stage 4 guide **I-07** (S4, `Stage4-ViewingSurface-Testing-Guide.md` §8), deferred as `DW-1` (`Deferred-Work.md`). |
| **Next action** | **The user's walk of all thirteen manual cases, M-01 … M-13** (`Stage6-Annotations-Testing-Guide.md` §6), **M-01 … M-04 passed and signed off 2026-10-20** (their walks raised I-23 … I-29, all closed, §8; M-03 made A4's product verdict `go`, `Tech-Stack-Plan.md` §3.3), **M-05 passed and signed off 2026-10-22** (its first walk raised I-30 and I-31, both closed) with its steps 4 to 6, added the same day for the user's three Comments pane changes I-32 … I-34 (closed) -- **M-06 passed and signed off 2026-10-28** (its walks raised I-38 and I-47, both closed, I-47 by the user's decision: Acrobat without Adobe's font pack draws no CJK typed comment, D6-7's bound) -- **M-07 passed and signed off 2026-10-27** (its walks raised I-39 … I-43, all closed, §8), **its step 7, added the same day for the user's three later stamp changes I-44 … I-46, walked: its first walk found the accent bar still drawn, I-46 reopened and closed again 2026-10-28, and the user's re-walk of the bar check the same day found it fixed** -- **M-08 passed and signed off 2026-10-28**, its first walk, no issue raised -- **M-09 … M-13 not walked**, every one walkable -- Stage 6's sign-off waits on them (§9 item 3). A-101's by-hand recording run passed 2026-10-28, with the user's leave. |
| **Owed** | **A2 at Stage 6 is `go, with named custom work`** (the user's choice, kept 2026-10-20) and **still outside the guide's §3.1 through the shipped surfaces** -- the scrolling pane p95 21.9–29.4 ms beside the shipped pencil, edit-to-pixels p95 201–505 ms (`Tech-Stack-Plan.md` §3.3); the two remaining causes are **`DW-4`** (`Deferred-Work.md`), unassigned. |
| **Latest tests** | **2026-10-28, the parallel test presets (`"jobs": 8`, convention 25), after builds of both presets: 1843/1843 on `x64-release` three times running (168–169 s each, against about 7 min serially) and once more after the three `*.openssl` script tests were made serial (193 s), and on `x64-debug` once before that fix (247 s, against about 10 min)**, five named skips — the battery case (no battery), Stage 4 guide A-85 `[I-14]` and A-94 `[I-23]` (real desktop), Stage 5 guide A-50 `[D5-10]` (system clipboard), Stage 6 guide A-101 `[A-101-record]` (microphone) — plus, on the debug run only, `ui.diagnostics_ignore_the_bus`, because the user's M-06 window held the bus (the trap in *Traps*); A-101's by-hand run passed 2026-10-28 (`Stage6-Annotations-Testing-Guide.md` §5); 25 tests `RUN_SERIAL` by `--show-only=json-v1`; `checks` clean -- `mutation-lint` twelve commands, `layer-lint` 670 files, `license-audit` five Qt modules; `gates` not re-run (nothing it builds changed, 137/137 at I-36); no `warning:` in either build (incremental) -- the last clean-build census, 0 over 434 objects, is T15's. Earlier runs: each task's row in `Implementation-Plan.md` §4 and the guide's §8 suites lines. |
| **Latest CI read** | Run **39322128947** on `423152f` (Stage 6 guide I-26 and M-02, `Stage6-Annotations-Testing-Guide.md` §8), all five jobs green, read from their logs 2026-10-20: Windows **1819**, `portable-asan` **993** with no sanitizer report and `qpdf` 71 files 0 failed, `gates` 123, each fuzz target 61 s with no finding. T15's own run, 39297379729 on `62f21d2`, read the same day for guide A-04. Fuzz soak **39748541751** on `47de01f`, read from its logs 2026-10-22: **`fuzz_annot_pipeline` out of memory after 2,184 runs** -- Stage 6 guide **I-36**, closed with I-37 the same day -- the other four targets clean; the soak over the fix is owed. Per-run figures: `Implementation-Plan.md`'s Stage 6 block and its Stage 0 block. |
| **Authority for status** | The stage status blocks in [`Implementation-Plan.md`](claude-plan/Implementation-Plan.md) §4 — task by task and deviation by deviation, with every issue — and each stage's testing guide, which owns its cases, results and issue log. **Read them rather than trusting a figure remembered from here.** |

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
| `claude-plan/Stage<N>-<Area>-Plan.md` | *Optional per stage.* One stage's task-level execution order, its stage-local design decisions (`D<N>-n`) and its blocking preconditions. **Seven exist:** `Stage0-Toolchain-Plan.md`, `Stage1-AppShell-Plan.md`, `Stage2-Rendering-Plan.md`, `Stage3-Mutation-Plan.md`, `Stage4-ViewingSurface-Plan.md`, `Stage5-TextSearch-Plan.md` (2026-09-29, its §0 answered the same day) and `Stage6-Annotations-Plan.md` (2026-10-04, its §0 answered the same day), each written before that stage's code. **A stage plan owns no product facts and no live status** — those stay in the four documents above. |
| `claude-plan/Stage<N>-<Area>-Testing-Guide.md` | How one stage is verified — **both** the automated pass I run and the manual pass you walk, plus that stage's **issue log** (§8). **Seven exist**, one for each of Stages 0 to 6, each written from its plan **before that stage's code**. **Each guide owns its own case counts, results, manual walks and issue states — its status line says which cases carry a result; read them there rather than from a figure quoted elsewhere.** Every guide has its own `A-nn`/`M-nn`/`I-nn` namespace, so a bare `I-34` means Stage 1's. Some cases are Linux jobs this machine cannot run (sanitizers, fuzzing) and are recorded as such rather than as pending. Do not write one for a stage that is not next; do not treat its absence as a gap. |
| `claude-plan/Shipped-Surface.md` | A reader's overview of **what the built product lets a person do** today, and what is **deliberately absent** so it is not reported as a defect (moved out of this file 2026-10-16). Read on demand. No per-task status and no test figures. |
| `claude-plan/Deferred-Work.md` | **Work a stage deliberately did not do and that no later stage's plan has taken in** (`DW-n`, created 2026-09-27): each item's ownership state, what exists today and what is absent. Not the capability (`Features.md`), not the finding (the raising guide's §8), not the repair. **When a stage's plan takes an item, record it there and mark the entry taken — never delete a row.** Four entries, none taken (re-read 2026-10-20): `DW-1`, substitute page colours (S4 guide I-07); `DW-2`, a vector snapshot (S5 plan Q3); `DW-3`, typed comments outside WinAnsi and CJK (S6 plan Q3); `DW-4`, an annotation burst inside A2's frame budget (S6 D6-24, T15). |

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
| What a shipped task lets a **person do**, as an overview; an absence **decided** rather than pending | `Shipped-Surface.md` §1 / §2 | `CLAUDE.md`, which points at it and stays narrow |
| The **current** stage, next action, owed work and latest figures | `CLAUDE.md` → *Where the work is right now*, each row **replaced** | a *"Before that, …"* chain — the superseded figure is already in `Implementation-Plan.md` §4 |

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
11. **The status table at the top of this file is REPLACED, never appended to.** When a row changes,
    the figure it held is already in its owner (the task's row in `Implementation-Plan.md` §4, the
    guide's status line); write the new one in its place, with no *"Before that, …"* chain and no
    re-derivation history in parentheses. A new capability goes to `Shipped-Surface.md`, not here.
    *(By 2026-10-16 the Tests cell alone held 32 KB of superseded runs and this file had reached
    158 KB, half of it the status and history it says it does not carry.)*

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
convention 15). This is what makes the same chord mean different things per context (thirteen deliberate
Write-mode collisions are specified in `Features.md` §8 — twelve until 2026-10-04, when Stage 6's Q1 made `Ctrl+Alt+M` a Viewer chord) and what makes the headless batch runner and
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
**Fifteen filesystem-write waivers are live and no literal waiver is** (re-derived 2026-10-20 at Stage 6 T15, unmoved since Stage 5 T9; why each later task added none is its row in `Implementation-Plan.md` §4) —
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

**Twenty-five of them, and the full text with the measurement behind each is
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
25. **A Catch2 case that grades a duration carries the tag `[run-serial]`** — the x64 test presets run ctest at `-j 8`, and a timing case sharing the machine measures its neighbours.

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
ctest --preset x64-release --output-on-failure        # 1843/1843 as of 2026-10-28 (S6 I-46 reopened), 5 skipped
                                                      #   at -j 8 from the preset, ~190 s (convention 25); `-j 1` for one at a time
```

The corpus is data, but it is **checked** like code — and the checks need Python, which is a
verification dependency and deliberately **not** a build one (`cmake --build` never calls it). They
run as `gates.corpus.*` in the `gates` preset; by hand:

```powershell
python corpus/make-corpus.py                     # regenerate all 108 PDFs, 36 JSON sidecars, 8 FDF/XFDF, 2 images (S6 T1 added 11 PDFs; S6 I-27, I-28 two exchange files)
python corpus/make-corpus.py --verify            # they regenerate BYTE-IDENTICALLY (D2-11, S2 T4)
python corpus/make-corpus.py --check-provenance  # every file has a PROVENANCE.md row, and vice versa
python corpus/make-corpus.py --self-test         # the negative control: each detection must fire
python corpus/make-corpus.py --check-faults      # S5: each text fixture carries what its row claims (A-14)
python corpus/make-compare.py --self-test        # S5: every comparison record re-derives its pair (A-15)
python corpus/make-annots.py                     # S6: writes ONLY the annotation fixtures, labels, exchange/ and stamps/
python tools/cjk-coverage/make-cjk-coverage.py   # S6 T7: REWRITES core/doc/CjkCoverage.inc, the CJK characters a typed
                                                 #   comment can show; --verify and --self-test are its two gate cases.
                                                 #   Narrow it in its EXCLUDED table, never by hand -- and only on the
                                                 #   evidence of `ctest -R "coverage table claims"` (render.)
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
.\build\x64-release\bin\rawpage.exe --list-actions        # 389 declared / 173 registered / 216 not (2026-10-20, S6 T15), + effective chords (A-72)
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
.\build\x64-release\bin\golden-update.exe --appearance --reason "..."   # S6 T8: golden/appearance/, our builder's streams
                                                                         #   (tests/golden/AppearanceCases); --filter <name>
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
                                                    #   `--repeat N` the Stage 5 runs (3). And since S6 T15
                                                    #   (D6-24, S6 guide A-104) `--stage6`: a window split
                                                    #   in two, the SHIPPED PENCIL drawing a stroke every
                                                    #   100 ms on page 450 in one pane while the other
                                                    #   scrolls, graded on the scrolling pane; `--seconds N`
                                                    #   for a short run (CTest's, `bench.surface.a2_stage6`)
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
                                                    #   `--stage6` is A2 AT STAGE 6 (S6 T4, D6-24): an
                                                    #   annotation edit every 100 ms reloading the engine
                                                    #   while another page scrolls, each viewport tile's
                                                    #   wait split into queued / engine / pump; 30 s a
                                                    #   file graded, `--seconds N` for a short run
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
.\build\x64-release\bin\layer-lint.exe    --root . --explain   # includes, rules, 15 live waivers (2026-10-20)
.\build\x64-release\bin\i18n-lint.exe     --root . --explain   # locales, shipped flags, unfinished count
.\build\x64-release\bin\mutation-lint.exe --root . --explain   # rules, blind spots, each command's row and tags
.\build\x64-release\bin\license-audit.exe --manifest vcpkg.json --spdx-map tools\license-audit\spdx-map.json `
    --qt-modules build\x64-release\qt-modules.txt --compiler-plugins build\x64-release\compiler-plugins.txt
    # license-audit has NO --explain and NO --help: an unknown or missing argument prints its usage and
    #   exits 2 (measured 2026-09-14). --target checks passes it these arguments, plus
    #   --vcpkg-installed when an installed tree exists -- cmake/RawPageChecks.cmake is the invocation
.\build\x64-release\bin\ribbon-validate.exe --explain   # NOT a gate -- a build step (D1-8)
```

**Four notes on running `rawpage.exe` from PowerShell**, each of which cost time to rediscover.
PowerShell does **not wait** on a GUI-subsystem binary, so `$LASTEXITCODE` after `& .\rawpage.exe`
reads the *previous* command's — use `Start-Process -Wait -PassThru` and read `.ExitCode`. **The same
missing wait garbles the terminal**: the prompt is drawn before the product's log, and PSReadLine redraws
it on the next key at the line it remembers, so the log is overwritten in place — launch a walk's build
with `.\scripts\run.ps1 <args>` (`-Preset x64-debug` for the debug one), which waits (Stage 6 guide
**I-35**). **Never with `Start-Process -NoNewWindow`**: that hands the child the console's handles, so
`app/main.cpp` never calls `AttachConsole` and a Ctrl+C never reaches it (measured 2026-10-22). A
Ctrl+C in the console asks the product to close its windows; a second while the first waits ends it
(`app/ConsoleInterrupt.h`). And an
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
analyzer and normalizer over synthetic hostile geometry -- and **`fuzz_annot_pipeline` from Stage 6 T2** (D6-29), the
annotation reader and store over the backend's first-party walk -- and, since Stage 6 T3, the appearance builder and a composed annotation update its own verify must accept. It needs clang and libFuzzer, neither of which is
on this machine, so it runs only on Linux CI, in two places: `ci.yml`'s `portable-fuzz` job runs each
target for 60 s on every push, and **`.github/workflows/fuzz-soak.yml` is the ten-minute soak** (guide
cases A-25 and A-26). The soak runs daily and by hand, never on push:
`gh workflow run fuzz-soak.yml -f seconds=600`. What each soak found is in the guide whose issue it
raised (Stage 3 I-13, Stage 5 I-23, Stage 6 I-17 and I-22). `tests/fuzz/CMakeLists.txt` fails configure when its target count is
wrong — **5 since Stage 6 T2** — asserted by `cmake/RawPageFuzzTargets.cmake`, whose
`gates.fuzz_count.*` cases prove it fires — because "ten minutes with no crashes" is passed trivially by a
target that builds nothing.

**Six test targets and eight benchmark targets** (re-derived from the `CMakeLists.txt` files
2026-10-09 at S6 T10; one of the eight is
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
ctest --preset x64-release -R "^platform\."      # the OS layer's suite (51 cases, re-counted 2026-10-09)
ctest --preset x64-release -R "^gates\."         # the gates against deliberately bad input
ctest --preset x64-release -R "unit.settings.compile"   # the negative-COMPILE fixtures (A-41)
ctest --preset x64-release -R reversibility     # apply+revert restores the structural hash (S3)
ctest --preset x64-release -R "instant" -V      # S5 A-63: text-900.pdf's second search, graded on the guide's 3.3 (~40 s)
ctest --preset x64-release -R "search_settings.restart" -V   # S5 A-68: the search/* keys across TWO processes
ctest --preset x64-release -R "every labelled change" -V      # S5 A-95: the comparer graded against corpus/compare/'s records
ctest --preset x64-release -R "unrelated documents finish" -V # S5 A-97: the bound -- seconds, live-heap peak, and the cap's control
ctest --preset x64-release -R roundtrip         # open, rotate, save, reopen over the corpus (S3 T8)
ctest --preset x64-release -R annotdisplay      # S6 T4: the live engine draws what the saved file draws (A-34 ... A-39)
ctest --preset x64-release -R a4_product -V     # A-57: the product saves the signed corpus, OpenSSL judges (S3 T8);
                                                #   and S6 guide A-33, a HIGHLIGHT saved instead (S6 T3), into a4-product-annot
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
  Filter by tag or by a `*wildcard*`, and refuse a run whose output has neither a `test cases:` line nor
  `All tests passed (... test cases)` -- an ALL-GREEN run prints only the second, so a runner that
  requires the first reports a plant that stayed green as *no case ran* (S6 T11's P7).
- **A gate case that repeats `EXPECT_MATCH` or `EXPECT_NOT_MATCH` asserts every one of them only since
  S5 T13** (S5 guide I-09): before, `cmake_parse_arguments` kept the last and two cases had each been
  checking one of two clauses. A plant that fails `clean_tree` and not the case written for it is the sign.
- **The x64 test presets run ctest at `-j 8`, so a case that grades a duration must be tagged
  `[run-serial]`** (convention 25), or it measures seven neighbours as well as itself. The
  `bench.*` rows are serial without a tag. A suspected race is reproduced with `-j 1` — and the
  parallel run's `LastTest.log` interleaves cases, so read a failure by its test number. **Two
  tests writing one fixed build-tree path race too**, and no tag finds that: the three `*.openssl`
  script tests are `RUN_SERIAL` for it. Measured 2026-10-28: about 7 min serially, 193 s in parallel.
- **An `x64-debug` rebuild that touches a layer library spends minutes in ONE link.** GNU ld took
  **269 s** to link `rawpage_ui_tests.exe` at `-O0` against 24 s for the same executable in
  release (2026-10-28), and 152 s against 2 s with debug info stripped at link — so what separates
  the presets is the symbol count of unoptimized objects (22,878 in `tst_shell.cpp.obj` against
  1,444), and every test and bench executable relinks. While iterating, build only the target being tested
  (`cmake --build --preset x64-debug --target rawpage_tests`).
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
- **A chord another program holds as a system-wide hotkey never reaches the product**, and the
  suite cannot see it. Every widget case sends its `QKeyEvent` straight to the widget, below Windows'
  input queue. On this machine the NVIDIA App's overlay held `Alt+R`, so the find bar's regex toggle
  acted as a bare `Alt` in M-06's walk (S5 guide **I-17**, 2026-10-03); it held `Ctrl+Alt+M` too, the
  sticky note's chord, and other programs held other `Ctrl+Alt` chords at times (S6 guide **I-23**,
  2026-10-20). **Sweep a walk's chords before handing it over**, not after a dead key. When a walker reports a chord
  that does nothing, try `RegisterHotKey` on it first: `ERROR_HOTKEY_ALREADY_REGISTERED` (1409) means
  another program owns it. A real-input reproduction is `SendInput` into a window on the `windows`
  platform, with the user's hands off the desktop.
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
- **A paint checked at a whole scale says nothing about this machine's screens, which run at 150 %.**
  A widget whose edge falls between device pixels (logical x 793 → device 1189.5) loses a hairline
  drawn on that edge to clipping, while a grab of the widget alone, `ctest`'s offscreen scale of 1, a
  `windows`-platform test process (not per-monitor DPI-aware, so also 1) and `QT_SCALE_FACTOR=1.5`
  (which landed on a whole pixel) all show it (S5 guide **A-123**, 2026-10-03). Reproduce the product's
  scale with `QT_QPA_PLATFORM=windows:dpiawareness=2`, or render into an image at 1.5 at an odd
  logical offset, as A-123's case does.
- **A hidden window's page views still ask for tiles.** A hidden comparison window's panes held 65
  tiles each (S5 T14), so hiding a window is not a way to keep its requests out of a measurement.
- **The `portable-asan` job's `-Werror` build can be reproduced here, though its sanitizers cannot.** MinGW's GCC 13.1
  COMPILES with `-fsanitize=address,undefined` (it cannot link it), and the sanitizer flags change what `-O2` warns about
  -- a one-element initializer list read as out of bounds (S6 guide **I-11**), `<regex>` before it (S3 guide I-08). Take
  an object's command from `ninja -C build/x64-release -t commands rawpage_tests` (a tree called `buildd-release`, which
  this line named until 2026-10-16, does not exist on this machine), replace `-O3` with `-O2`, add `-fsanitize=address,undefined
  -Werror`, and compile it before pushing a change to `core/`, `pdf/cos/` or `pdf/ops/`. **A large object stops in the
  ASSEMBLER, not the compiler** -- *"too many sections"*, *"file too big"*: the instrumented `pdf/ops/MutationSinkImpl.cpp`
  passes COFF's section limit (measured at S6 T14), which ELF on CI's Linux runner does not have -- so add `-Wa,-mbig-obj`
  for that file and read the compiler's own verdict.
- **A warning census from an incremental build is inherited, not measured** — a built tree compiles
  nothing and prints *"ninja: no work to do"*. Use `--clean-first` when the claim is about warnings
  (S3 guide A-01). The object count grows every task — 434 C++ objects on `x64-debug` at Stage 6
  T15 — so re-derive it from the clean build's log rather than quote it.
- **A target that does not call `rawpage_common_flags` is not compiling this project's language, and
  it will build until the day it does not.** Four targets had been compiled at the compiler's default
  standard — `rawpage_fuzz_seeds` since Stage 3 T13, the three `tests/fuzz` drivers since Stage 2 T5,
  those three because they skip `rawpage_strict()` to avoid `-Werror` and lost the standard with it.
  **`rawpage_assert_language_standard(20)` now refuses to configure such a target**, so this is a gate
  rather than a thing to remember (S4 guide **I-08**). Two bounds: it checks the standard alone, and it
  walks only the graph the current preset configures — the three fuzz drivers exist only under
  `RAWPAGE_BUILD_FUZZERS`, so a Windows configure walks **33** targets (the configure's own `targets walked`
  line, 2026-10-09) and that
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
is a first-party reflow engine). **A4** (does a signature survive a later unrelated incremental
save) is **SETTLED `go`** on its probe (Stage 3) and through the product (Stage 6's M-03, 2026-10-20), both over
RawPage's generated signatures only (`Tech-Stack-Plan.md` §3.3). **A5**, **A6** and **A7** were settled in Stage 0 — we build with Qt's own bundled GCC
13.1.0 and `toolchain-mingw.cmake` keeps it that way. Risks **R5** and **R6** are retired with A2 and
A3. Larger unknowns carried forward: the Write-mode reflow engine (R1), a first-party full-text index
since Xapian is GPL (R2), Office export fidelity (R3), dynamic XFA (R4), the sanitizer/PDB capability loss from the
GCC toolchain (R7), and the MSVC-shaped corners of the Windows integration surface — print driver,
Office add-ins, arm64 (R8). Open decisions are **O1, O3, O4, O5, O6, O8 and O11** (O2, O7, O9 and O10
are resolved), listed with their owning stages in `Implementation-Plan.md` §8 — if work forces one,
record the resolution there rather than deciding it inline. *(States re-read from the owning tables on
2026-10-09, unmoved since 2026-09-14; read them there.)*
