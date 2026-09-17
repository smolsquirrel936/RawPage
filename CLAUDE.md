# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**This file is a ROUTING document.** It holds what binds every session — the locked stack, the
architecture, the machine-enforced rules, the commands — and points at the document that owns
everything else. It deliberately does **not** carry per-task history, per-file descriptions or live
status; those have owners, listed in the document-set table below, and a second copy here is how a
number goes stale. When something changes, **fix the owning document**; never re-decide a fact here.

## What this repository is

**RawPage** — a Windows-first desktop PDF editor modeled on Right PDF Professional. The design lives
under `claude-plan/`. Stage 0 (toolchain), Stage 1 (app shell) and Stage 2 (PDF engine boundary and
tiled rendering) are complete and signed off, the last on 2026-09-14. **Stage 3 is in progress: all thirteen tasks are done (T13 on 2026-09-17) and the stage awaits sign-off; risk A4's probe verdict is `go`, and its product verdict is recorded as partial.**

### Where the work is right now — read this first

| | |
|---|---|
| **Stage** | **Stage 2 is SIGNED OFF by the user, 2026-09-14** — code complete 2026-09-06, manual walk complete and every `Stage2-Rendering-Testing-Guide.md` §9 item met 2026-09-14. **Stage 3 is in progress** — its plan and testing guide were written 2026-09-14, before any Stage 3 code, and **T1 was completed the same day**. **T2 was completed the same day, with A4's probe verdict `go`, and T3 and T4 after it, T5 to T10 on 2026-09-15, and T11 and T12 on 2026-09-16, and T13 on 2026-09-17.** |
| **Next action** | **Stage 3's sign-off** (`Stage3-Mutation-Testing-Guide.md` §9) — every task is done, and **one item is open: item 1**, the automated cases never ticked in the guide (A-01, A-02, A-06, A-07, A-12, A-13 — run and record them). **Item 2 is met as of 2026-09-18: every manual case is signed off** — M-01, M-02, both M-03 walks and M-04 … M-07 on 2026-09-17, M-08 on 2026-09-18 (§6). **M-08's walk found Stage 3 guide I-15** (S2, fixed the same day): a save refused by the file system — read-only, or Acrobat's handle — showed a banner and never offered Save As, where **D3-16** (`Stage3-Mutation-Plan.md` §3) promises a named error **and** the offer; `SaveCoordinator` now returns `SaveAsRequired` carrying the refusal when the FILE SYSTEM refused, and an error when `RevisionWriter` refused our own bytes — which no other path would take either. **It also measured what A-48 could not:** Acrobat holds a PDF **without `FILE_SHARE_DELETE`**, so it refuses the replace (`error.file.inUse`, `win32 error 32`), and the file was byte-exact after every refusal. **Item 7 is met**: A4's product verdict is recorded as PARTIAL, its rotation control met by M-03's product walk and its permitted edit Stage 6's. **M-02's walk found Stage 3 guide I-14** (S1, closed): Acrobat reads `/XRefStm` only from a trailer that has `/Prev`, so every save of a hybrid file was damaged in Acrobat — the writer now carries the key, and `corpus/ordinary/hybrid-xrefstm.pdf` is written in Word's two-section layout. **T13 closed A-83** (2026-09-17): the re-soak `fuzz-soak.yml` run 35137107236 on `dae5357` was clean on all three targets, read from its log — the round trip 25,472 inputs in 612 s. **Its first soak, run 35134357858, had found Stage 3 guide I-13** (S2, fixed): an object number from the file placed with no bound, overflowing `largest + 1`; the reader now never places one above `cos::kMaxObjectNumber` (8,388,607); and the user's remaining manual walk, M-08 (§6) — then Stage 3's sign-off. **T13 left** (2026-09-17): `fuzz_cos_roundtrip` (D3-17) and its body `tests/fuzz/RoundtripFuzzBody.h`, replayed by `tst_roundtrip`; `rawpage_fuzz_seeds` (`tests/fuzz/seeds/`), the writer's outputs as seeds; the target count asserted by `cmake/RawPageFuzzTargets.cmake` and proven to fire by `gates.fuzz_count.*`; and **A4's product verdict recorded as PARTIAL** in `Tech-Stack-Plan.md` §3.3 — integrity through the product's Save met, its Acrobat rotation control met by M-03's product walk on 2026-09-17 (each saved update byte-identical to the probe's), a permitted edit Stage 6's. **Before its first push the body found Stage 3 guide I-12** (S2, closed): four ways an append changed what a REPAIRED file's original bytes mean — an object running to end-of-file absorbing the update, a dangling reference given the new xref stream's number, a stream page rewritten as a dictionary, an object 0 recovered — fixed by `IncrementalWriter::protectOriginal`, `PageOps::rotatedPage` and `XrefRepair`. **A fuzz finding in the round trip is reproduced by adding its bytes to a `roundtrip I-12`-style case in `tests/roundtrip/tst_roundtrip.cpp`, which runs on Windows and under `portable-asan`.** **T12 left it** (2026-09-16): `documents/recoveryDir` and `documents/warnOnRepairedSave` are declared, round-trip, and ship on the generated **Documents** Preferences page (six of 26). The recovery folder is `RecoveryJournal::resolveRoot` — the key when ABSOLUTE, then `Services::recoveryRoot`, then `%LOCALAPPDATA%\RawPage\recovery` — read when a document opens and by the launch prompt, which looks there only. The repaired-document key skips the explanation and never the Save-As, and silences no other reason. **T11 left the recovery prompt** (commit `bd5543d`, pushed; CI run 35029614272 green on all five jobs): Recover, Discard or Decide later per crashed document, an untitled document when the file changed, and A-74 in the sixth test binary `tests/composition/` (`ui.composition.`). **A case that composes the product must point `Options::recoveryRoot` at a scratch directory**, or the suite writes into, and is offered, the recoveries of whoever runs it. **A planted-bug control on a `ui/` file rebuilt through one test target leaves the other binaries that link `rawpage_ui` stale** — rebuild everything before a full run (T11 row). T10 wired undo, redo, rotate, save and the close dispositions. **A widget case that closes a document must flush `QEvent::DeferredDelete` before its fixture goes** — the tab's destructor closes its handle on whatever backend is at that address (T10 row, `Implementation-Plan.md` §4). **A `command.*` catalog entry must sit in a `command` context** — `translate` looks in the key's namespace and no gate checks the context (Stage 3 guide I-10). T9 built `RecoveryJournal` and the kill harness; T8 the save path; T7 `platform::IDurableFiles`; T6 `mutation-lint`; T5 `Command` and `CommandJournal`; T4 `Document` and `Permissions`; T3 the sink. **A failed Catch2 `CHECK` on two `QByteArray`s crashes the reporter** — compare hashes (Stage 3 guide I-09). **A planted-bug control that restores a file by moving a backup back leaves ninja's build stale** — touch the restored file (T5 row, `Implementation-Plan.md` §4). **A probe output open in a PDF viewer makes the A4 probe's write fail** — close `build\<preset>\a4-probe\` files before running the suite (T3 row, `Implementation-Plan.md` §4). Guide case M-03's probe walk (`Stage3-Mutation-Testing-Guide.md` §6) was signed off by the user on 2026-09-14; its product walk on 2026-09-17. **Where T2 left A4 (2026-09-14):** `pdf/cos/IncrementalWriter` wrote a comment and a rotation into each `corpus/signed/` file; every original is the unchanged prefix of its output on disk; OpenSSL rejected every flipped-byte copy and verified every signature; and Acrobat reported every approval signature's signed revision unaltered after the comment, while flagging both controls — **`go`, over RawPage's generated signatures** (`Tech-Stack-Plan.md` §3.3). The first walk found Stage 3 guide **I-06**: Acrobat rejects a page ROTATION after these signatures from any writer, so a RawPage rotation of a signed PDF will read INVALID in Acrobat (policy, Stage 11's to honour), and the product's permitted-edit Acrobat check moved to Stage 6. T2 also found and fixed two reader defects, **I-04** and **I-05**; its row in `Implementation-Plan.md` §4 carries the counts and deviations. The order, the decisions (**D3-1 … D3-18**) and the preconditions (**P-1 … P-5**) are in [`Stage3-Mutation-Plan.md`](claude-plan/Stage3-Mutation-Plan.md) §3, §6 and §7; how each task is verified is [`Stage3-Mutation-Testing-Guide.md`](claude-plan/Stage3-Mutation-Testing-Guide.md). **Four of those decisions are the user's, 2026-09-14** — Save writes a complete new file and atomically replaces the target (D3-4); A4 is judged by the OpenSSL command-line tool and Acrobat over generated signed files (D3-2); `Outline` and `Metadata` move to Stage 4 (D3-8); two of the four planned settings keys move to Stages 8 and 13 (D3-9). *How Stage 2 closed, 2026-09-14:* the manual walk was complete, all ten cases signed off by the user, the last (M-09) on 2026-09-14. Item 5 was met by the ten-minute fuzz soak, `.github/workflows/fuzz-soak.yml` run 34781013867 (A-25 and A-26, nothing found). Item 1 was met 2026-09-14: A-72 and A-73 were completed (the second finding Stage 2 **I-39**), and A-69 was met when the product benchmark gained its latency control, which found Stage 2 **I-41**. A-71 is met, and clean since CI run 34775047037 — the first whose fuzz job aborts on a sanitizer finding (Stage 2 **I-38**; every fuzz run before it had logged one under a green tick). I-35 is closed. |
| **Then** | Stage 3's sign-off — every item in `Stage3-Mutation-Testing-Guide.md` §9, the manual walks signed off by the user — and then Stage 4, whose plan and testing guide are written before its code. |
| **Authority for status** | The **Stage 1, Stage 2 and Stage 3 status blocks** in [`Implementation-Plan.md`](claude-plan/Implementation-Plan.md) §4 — task by task and deviation by deviation, with every issue. **Read them rather than trusting a figure remembered from here.** |
| **Tests** | **923/923** on `x64-release` and **923/923** on `x64-debug` as of Stage 3 guide I-15's fix (measured 2026-09-18), one explicitly skipped (the battery case, on a desktop that has none). The `gates` preset is 124/124 with no Qt, measured the same day on Windows. Re-measure rather than quote: a count in prose goes stale silently. |

**What works today.** `rawpage.exe` opens real PDFs into tabs and draws their pages: a ribbon
(9 tabs, 192 controls), a customizable Quick Access Toolbar, a tool finder, document tabs, a
keyboard, a Preferences dialog, a log pane and a live theme. A person can scroll, page through with
the keyboard, zoom by chord or `Ctrl`+wheel, fit four ways, rotate the view, switch the four layout
modes, type a page number, pan, marquee-zoom, and read a banner saying a file was repaired. The whole
**read** path is connected — `pdf/cos/` (lexer, objects, nine filters, xref, repair), `pdf/render/`
(the PDFium facade), `core/render/` (every decision about a tile except its pixels), `ui/viewer/`
(tiles onto a widget). **The save path exists below the UI** since Stage 3 T8 — `CommandJournal`,
`SaveCoordinator`, `pdf/ops/RevisionWriter` and `platform::IDurableFiles` save a rotation over the
whole editable corpus in tests — and **since T10 a person reaches it**: `Ctrl+R` rotates pages, `Ctrl+Z`/`Ctrl+Y` undo and redo,
`Ctrl+S`/`Ctrl+Shift+S` save, and closing a changed document asks first. **Since T11 a launch after a crash offers what was not saved**: recover it — into the file, or into an untitled document when the file changed — discard it, or decide later.

**What is deliberately absent, so it is not reported as a defect.** `platform::IAcceleratedSurface`
is declared and unimplemented (Stage 4, *if A2 says it is needed* — it does not). Font substitution
runs through our own policy but has **no UI**: only the log names the face. `pageDisplay/lineWeight`
is a checkbox that does nothing — **I-06** and **I-11** (`Stage2-Rendering-Testing-Guide.md` §8) are
both open **by decision**, each a feature the engine or the licence cannot deliver, kept as a
declared-and-inert surface so there is a row to fill in the day the constraint lifts. Thirteen §8 Viewer chords (re-counted 2026-09-15, Stage 3 T10) stay `NotFound` and deliberately leave the key unconsumed. `Select` arms a tool that selects
nothing until Stage 5. **308 of 361 declared commands are unregistered**, which is the honest state
rather than a shortcut.

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
crash. **I-05 is closed**; the engine lock in `PdfiumInit` stays.

## The document set — who owns which facts

Each document is the single source of truth for its own subject; the others cross-reference rather
than restate. **When something changes, fix the owning document; never re-decide a fact locally.**

| Document | Owns |
|---|---|
| `claude-plan/Features.md` | The capability catalog and the **authoritative key-binding tables** (§8). No stack choices. |
| `claude-plan/Tech-Stack-Plan.md` | Locked stack, license posture, directory layout, the inward-only dependency rule, mutation-through-commands, the first milestone. |
| `claude-plan/Architecture-FileMap.md` | What every planned file does, plus the call-trace flows A–K. |
| `claude-plan/Implementation-Plan.md` | Stage ordering, **live status**, shared conventions, testing strategy, Definition of Done, traceability. |
| `claude-plan/Stage<N>-<Area>-Plan.md` | *Optional per stage.* One stage's task-level execution order, its stage-local design decisions (`D<N>-n`) and its blocking preconditions. **Four exist:** `Stage0-Toolchain-Plan.md`, `Stage1-AppShell-Plan.md`, `Stage2-Rendering-Plan.md` and `Stage3-Mutation-Plan.md`, each written before that stage's code. **A stage plan owns no product facts and no live status** — those stay in the four documents above. |
| `claude-plan/Stage<N>-<Area>-Testing-Guide.md` | How one stage is verified — **both** the automated pass I run and the manual pass you walk, plus that stage's **issue log** (§8). **Four exist**, one per stage built or next: `Stage0-Toolchain-Testing-Guide.md` (run, signed off, closed), `Stage1-AppShell-Testing-Guide.md` (fully run and fully walked, issues I-01…I-55, none open), `Stage2-Rendering-Testing-Guide.md` (automated half run; manual half **fully walked and signed off**, the last case on 2026-09-14; every §9 item met, and the stage signed off by the user, on 2026-09-14) and `Stage3-Mutation-Testing-Guide.md` (written 2026-09-14 from its plan; **every task's cases run through T13**, six early automated cases never recorded, M-01, M-02, both M-03 walks and M-04 … M-07 signed off by the user, M-08 not yet walked). **Each guide owns its own case counts, results and issue states — read them there rather than from a figure quoted elsewhere.** Every guide has its own `A-nn`/`M-nn`/`I-nn` namespace, so a bare `I-34` means Stage 1's. Some cases are Linux jobs this machine cannot run (sanitizers, fuzzing) and are recorded as such rather than as pending. Each is written right after its stage's plan and **before that stage's code**, per the workflow section below. Do not write one for a stage that is not next; do not treat its absence as a gap. |

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
convention 15). This is what makes the same chord mean different things per context (twelve deliberate
Write-mode collisions are specified in `Features.md` §8) and what makes the headless batch runner and
CLI possible without a second implementation.

## Non-negotiable rules when writing code

These are machine-enforced by CI gates, not by review discipline. **Four are specified, and all four
are live** — the fourth, `mutation-lint`, from Stage 3 T6. `checks` runs four and says four; a count it
names that disagrees with the commands it runs is the bug.

| Gate | State | Fails on |
|---|---|---|
| `tools/layer-lint` | ✅ **live** | an outward include; **`FPDF_` outside `pdf/render/`** — narrowed from `pdf/` at S2 T2 (D2-9), because `pdf/cos/` is first-party by rule and is half of what the portable presets build on Linux — and, as a companion rule, **a PDFium HANDLE named outside `pdf/render/`**, which catches `ScopedFPDFDocument` and the deleter family that contain no underscore after `FPDF` and slip past the first rule entirely; a filesystem write outside `pdf/`, `core/settings/`, `tools/` and `tests/` — the last two because a gate and a test fixture write by definition; a path read out of `argv` outside `app/main.cpp`; **a `QAction` constructed outside `ui/shell/ActionFactory.cpp`** (and `RawPageAction.h`, which is the type the factory returns) (D1-7, four spellings, non-escapable); **an OS header in `platform/platform.h`** and **an engine or QtGui include in `core/backend.h`**, each its own layer entry because those files are included by three or four layers apiece |
| `tools/license-audit` | ✅ **live** | any dependency SPDX identifier outside the allowlist — **and from S2 · T7 it audits the RESOLVED CLOSURE**, enumerating every `share/*/vcpkg.spdx.json` in the installed tree rather than only what `vcpkg.json` declares (issue I-02). An installed tree *is* the closure, so this reads what we ship instead of keeping a second hand-maintained list; with **no** installed tree the enumeration is inert and the map still enforces the rule, which is what keeps the CI `gates` job honest. Two fixtures with a control prove it fires and does not simply fail on every tree |
| `tools/i18n-lint` | ✅ **live** | a literal reaching a user-facing sink and not wrapped in `tr()` (escapable, `// i18n-lint: allow-literal(reason)`); a key missing from a shipped locale; an `ActionId` with no `action.*` catalog entry. `orphan-key` is S3 — reported, never blocking, and a quoted key reference **ending in a dot** is a namespace reference covering every key beneath it, because several catalog families are built from a table at runtime and appear whole in no source (T14, issue I-27). Stage 1 ships `en` and `zh-TW` only; `zh-CN` and `ja` are declared `"shipped": false` (D1-14) |
| `tools/mutation-lint` | ✅ **live** (S3 · T6) | a class deriving from `Command` under `core/cmd/commands/` with **no `core/cmd/Commands.def` row**, **no Catch2 test case tagged `[serialize:Class]`** or **no test case tagged `[revert:Class]`** under `tests/` (`tests/gates/` excluded); and a table row whose header column does not declare its class — a column the compiler never reads. Read as text with the standard library only (D3-12). **It proves a test exists, not that it tests anything**: a revert that lies is `tst_reversibility`'s to catch (Stage 3 guide A-44), and `--explain` lists what the gate cannot see |

Two rules are escapable, both with an inline marker **on the same line as the match** — not the
line above — and both censused by that gate's `--explain`:
`// layer-lint: allow-filesystem-write(reason)` and `// i18n-lint: allow-literal(reason)`.
**Fourteen filesystem-write waivers are live and no literal waiver is** (re-derived at Stage 3 T9) —
three in `app/CrashHandler.cpp`, two in `app/LogFile.cpp`, two in `core/render/DiskTileStore.cpp`
(one write and one delete, because every deletion in that file is funnelled through a single helper
to keep the census at two), one on the named-pipe `CreateFileW` in
`platform/win/WinSingleInstanceBus.cpp`, four in `platform/win/WinDurableFiles.cpp` — one per
call spelling: `CreateFileW`, `ReplaceFileW`, `MoveFileExW` and `DeleteFileW` — and two in
`core/cmd/RecoveryJournal.cpp`, the journal's append and its whole-file write. **Run `--explain` rather than trusting that list**: it
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

**Twenty-three of them, and the full text with the measurement behind each is
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
ctest --preset x64-release --output-on-failure        # 923/923 as of guide I-15's fix, 1 skipped
```

The corpus is data, but it is **checked** like code — and the checks need Python, which is a
verification dependency and deliberately **not** a build one (`cmake --build` never calls it). They
run as `gates.corpus.*` in the `gates` preset; by hand:

```powershell
python corpus/make-corpus.py                     # regenerate all 67 files (S3 T1 added six)
python corpus/make-corpus.py --verify            # they regenerate BYTE-IDENTICALLY (D2-11, S2 T4)
python corpus/make-corpus.py --check-provenance  # every file has a PROVENANCE.md row, and vice versa
python corpus/make-corpus.py --self-test         # the negative control: each detection must fire
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
.\build\x64-release\bin\rawpage.exe --list-actions        # 361 declared / 53 registered / 308 not (S3 T10), + effective chords (A-72)
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
.\build\x64-release\bin\rawpage_bench_pipeline.exe # A2's PRODUCT figure (S2 T18), through the shipped
                                                    #   scheduler and cache -- plus O7's and D2-14's
                                                    #   numbers. `8` sweeps the worker count;
                                                    #   `--whole-page` reports the other quantity.
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
.\build\x64-release\bin\layer-lint.exe    --root . --explain   # includes, rules, 14 live waivers (S3 T9)
.\build\x64-release\bin\i18n-lint.exe     --root . --explain   # locales, shipped flags, unfinished count
.\build\x64-release\bin\mutation-lint.exe --root . --explain   # rules, blind spots, each command's row and tags
.\build\x64-release\bin\license-audit.exe --manifest vcpkg.json --spdx-map tools\license-audit\spdx-map.json `
    --qt-modules build\x64-release\qt-modules.txt --compiler-plugins build\x64-release\compiler-plugins.txt
    # license-audit has NO --explain and NO --help: an unknown or missing argument prints its usage and
    #   exits 2 (measured 2026-09-14). --target checks passes it these arguments, plus
    #   --vcpkg-installed when an installed tree exists -- cmake/RawPageChecks.cmake is the invocation
.\build\x64-release\bin\ribbon-validate.exe --explain   # NOT a gate -- a build step (D1-8)
```

**Three notes on running `rawpage.exe` from PowerShell**, each of which cost time to rediscover.
PowerShell does **not wait** on a GUI-subsystem binary, so `$LASTEXITCODE` after `& .\rawpage.exe`
reads the *previous* command's — use `Start-Process -Wait -PassThru` and read `.ExitCode`. And an
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
every preset, `tests/fuzz/seeds/`). It needs clang and libFuzzer, neither of which is
on this machine, so it runs only on Linux CI, in two places: `ci.yml`'s `portable-fuzz` job runs each
target for 60 s on every push, and **`.github/workflows/fuzz-soak.yml` is the ten-minute soak** (guide
cases A-25 and A-26). The soak runs weekly and by hand, never on push:
`gh workflow run fuzz-soak.yml -f seconds=600`. Its first run, 34781013867, was clean over two targets; over three,
run 35134357858 found Stage 3 guide I-13 and run 35137107236, on the fix, was clean. `tests/fuzz/CMakeLists.txt` fails configure when its target count is
wrong — **1 at T5, 2 from T6, 3 from Stage 3 T13**, asserted by `cmake/RawPageFuzzTargets.cmake`, whose
`gates.fuzz_count.*` cases prove it fires — because "ten minutes with no crashes" is passed trivially by a
target that builds nothing.

**Six test targets and six benchmarks**, and the split is load-bearing rather than
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
ctest --preset x64-release -R "^platform\."      # the OS layer's suite (36 cases, re-counted 2026-09-14)
ctest --preset x64-release -R "^gates\."         # the gates against deliberately bad input
ctest --preset x64-release -R "unit.settings.compile"   # the negative-COMPILE fixtures (A-41)
ctest --preset x64-release -R reversibility     # apply+revert restores the structural hash (S3)
ctest --preset x64-release -R roundtrip         # open, rotate, save, reopen over the corpus (S3 T8)
ctest --preset x64-release -R a4_product -V     # A-57: the product saves the signed corpus, OpenSSL judges (S3 T8)
ctest --preset x64-release -R "platform.recovery.kill" -V   # A-68, A-69: TerminateProcess x20 during commands and saves, and where each kill landed (S3 T9)
ctest --preset x64-release -R "ui.composition" -V          # A-74: a killed probe's journal offered and replayed by CompositionRoot + MainWindow (S3 T11)
ctest --preset x64-release -R "roundtrip (fuzz body|I-12)" -V   # A-83: fuzz_cos_roundtrip's body over the corpus, its controls, and I-12's four regressions (S3 T13)
ctest --preset gates -R fuzz_count                         # A-83(a): the fuzz target count assertion refuses 2 of 3, 0, and a helper standing in
```

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
is a first-party reflow engine) and **A4** (does a signature survive a later unrelated incremental
save, Stage 3) — whose probe verdict is `go` and whose product verdict is recorded as partial, both over
RawPage's generated signatures only (`Tech-Stack-Plan.md` §3.3). **A5**, **A6** and **A7** were settled in Stage 0 — we build with Qt's own bundled GCC
13.1.0 and `toolchain-mingw.cmake` keeps it that way. Risks **R5** and **R6** are retired with A2 and
A3. Larger unknowns carried forward: the Write-mode reflow engine (R1), a first-party full-text index
since Xapian is GPL (R2), Office export fidelity (R3), dynamic XFA (R4), the sanitizer/PDB capability loss from the
GCC toolchain (R7), and the MSVC-shaped corners of the Windows integration surface — print driver,
Office add-ins, arm64 (R8). Open decisions are **O1, O3, O4, O5, O6, O8 and O11** (O2, O7, O9 and O10
are resolved), listed with their owning stages in `Implementation-Plan.md` §8 — if work forces one,
record the resolution there rather than deciding it inline. *(States re-read from the owning tables on
2026-09-14; read them there.)*
