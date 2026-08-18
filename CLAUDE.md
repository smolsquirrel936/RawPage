# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository currently is

**A design-document set plus Stage 0 build scaffolding — no product code yet.** The product is
**Recto** (working codename, open decision O9), a Windows-first desktop PDF editor modeled on Right
PDF Professional. The design lives under `claude-plan/`.

**Stage 0 is built and its automated verification is green; its manual pass is not walked, so it is
not done.** What exists is scaffolding, not product:

- the build system and six CMake presets, with the toolchain pin (TA-1…TA-9) refusing any compiler
  but Qt's own GCC 13.1.0/msvcrt;
- **two live gates** — `tools/layer-lint` and `tools/license-audit` — plus 25 CTest fixtures that
  prove they fail correctly, negative controls included;
- the four `core/` vocabulary headers (`Result.h`, `Log.h`, `Geometry.h`, `Units.h`) and a Catch2
  suite; **52 tests on Windows, 45 on Linux under ASan+UBSan**;
- CI on three jobs, the Windows one self-hosted;
- `recto.exe`, which links all four layers and **opens no window**.

**No document code exists**: nothing in `core/doc/`, `core/cmd/`, `pdf/cos/`, `pdf/render/`,
`pdf/ops/`, `ui/`, `platform/win/`, or `app/` beyond one placeholder TU per layer. Both Stage 0 risk
verdicts are recorded in `Tech-Stack-Plan.md` §3.3: **A5** and **A6**.

The documented next action is **Stage 0** (toolchain skeleton + the four enforcement gates) in
`claude-plan/Implementation-Plan.md`, now planned in full: `claude-plan/Stage0-Toolchain-Plan.md`
holds its tasks T1–T15, and `claude-plan/Stage0-Toolchain-Testing-Guide.md` holds its verification.
**All fifteen Stage 0 tasks are complete.** The next action is the **manual pass** —
`Stage0-Toolchain-Testing-Guide.md` §6, cases M-01…M-07, walked by the user on real hardware. I never
mark those passed on their behalf, and Stage 0's Definition of Done is not met until they are.
**Stage 1 does not start before that sign-off.** Do not skip ahead to feature work — the stage order
is deliberate risk retirement, not a backlog.

`claude-plan/skills-specs-generation/` is a **vendored third-party skill set** (its own nested git
repo, MIT-ish LICENSE) that generated these docs. Don't edit it as part of product work.

## The document set — who owns which facts

Each document is the single source of truth for its own subject; the others cross-reference rather
than restate. **When something changes, fix the owning document; never re-decide a fact locally.**

| Document | Owns |
|---|---|
| `claude-plan/Features.md` | The capability catalog and the **authoritative key-binding tables** (§8). No stack choices. |
| `claude-plan/Tech-Stack-Plan.md` | Locked stack, license posture, directory layout, the inward-only dependency rule, mutation-through-commands, the first milestone. |
| `claude-plan/Architecture-FileMap.md` | What every planned file does, plus the call-trace flows A–K. |
| `claude-plan/Implementation-Plan.md` | Stage ordering, **live status**, shared conventions, testing strategy, Definition of Done, traceability. |
| `claude-plan/Stage<N>-<Area>-Plan.md` | *Optional per stage.* One stage's task-level execution order and its stage-local design choices. **Only `Stage0-Toolchain-Plan.md` exists** (tasks T1–T15, decisions D0-1…D0-8, the §7 precondition checklist). It owns no product facts and no live status — those stay in the four documents above. |
| `claude-plan/Stage<N>-<Area>-Testing-Guide.md` | How one stage is verified — **both** the automated pass I run and the manual pass you walk. **Only `Stage0-Toolchain-Testing-Guide.md` exists**, written from Stage 0's plan before its code; no case in it has been run. Each is written right after its stage's plan and before that stage's code, per the workflow section below. Do not write one for a stage that is not next; do not treat its absence as a gap. |

`claude-plan/README.md` is an index only and owns no facts.

**Both `Architecture-FileMap.md` and `Implementation-Plan.md` carry live status and must be updated
in the same change as the code they describe** — a file's ○ → ✅ row flip is part of that file's
definition of done. A folded-away file keeps its row as a ⤵ breadcrumb; rows are never deleted.

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
  Xpdf, Qt Charts/DataVisualization, Xapian). Recto's own source is proprietary. **One named
  exception:** the GCC runtime (`libstdc++`, `libgcc`) is `GPL-3.0-or-later WITH GCC-exception-3.1`,
  which permits proprietary distribution **only while we compile with stock GCC** — adding a
  non-free GCC plugin voids the exception and would relicense our own source. `license-audit` matches
  whole SPDX expressions, never a `GPL` substring, and ships the runtime DLLs dynamically.
- **CMake ≥ 3.25 + presets + vcpkg manifest mode (`x64-mingw-dynamic`) + Ninja**; Catch2 v3;
  MSI (WiX v4) + MSIX, shipping the GCC runtime DLLs beside the Qt ones.
- **Sanitizers and fuzzing do not run on Windows.** MinGW GCC has no ASan/UBSan runtime and no
  libFuzzer, so the `portable-asan` / `portable-fuzz` presets build `core/` + `pdf/cos/` — the two
  layers that are portable by rule — on Linux instead. A finding there is reproduced in `x64-debug`
  before it counts as fixed. Debug info is DWARF, not PDB: crash dumps symbolize against archived
  unstripped binaries.
- **Async tiled rendering** at physical DPI on a worker pool; the UI thread never parses or rasterizes.

## Architecture (once code exists)

Layers are separate static libraries (`recto_core`, `recto_pdf`, `recto_platform`, `recto_ui`) linked
by one `recto` executable, so the linker itself catches some violations. **Dependencies point inward
only:**

- `app/` — composition root. The **only** translation units naming `pdf::createBackend()` or
  `platform::createPlatform()`. No domain logic.
- `ui/` — Qt Widgets only. May never include `pdf/` or `platform/win/`, and may never mutate a
  `Document`; its entire mutation vocabulary is *construct a `Command`, hand it to `CommandJournal`*.
- `core/` — domain. std + `QtCore` (documented exception) + a short pure-computation allowlist (ICU,
  HarfBuzz, Hunspell) + its two interface headers (`core/backend.h`, `platform/platform.h`). Must
  build and pass its full test suite with **no display and no PDF engine linked**.
- `pdf/` — the only layer that may emit PDF bytes or call `FPDF_*`. `pdf/cos/` is first-party
  (lexer, object model, xref, filters, crypt, incremental writer); `pdf/render/` is the PDFium
  facade; `pdf/ops/` realizes each mutation.
- `platform/win/` — the only OS-specific code; implements the 16 `platform/platform.h` interfaces
  and makes no policy decisions.
- `i18n/`, `res/`, `corpus/`, `golden/` are **data, not code**.

### Mutation-through-commands — the load-bearing design

Every document change is a `Command` (`name/scope/apply/revert/serialize`) run through
`CommandJournal`. `apply()` expresses **intent** to a `MutationSink` interface (`setPageRotation(3, 90)`)
— it never touches bytes; `pdf/ops/` decides how intent becomes COS objects. Read flow D in
`Architecture-FileMap.md` §5 before writing any mutating code; every mutation in the product, from a
highlight to Write-mode typing, has exactly that shape.

Consequences that fall out of it: undo/redo, an fsync-per-command recovery journal
(`%LOCALAPPDATA%\Recto\recovery\<docid>\`), precise `Scope`-keyed invalidation, and incremental save
(append a new xref section; Save-As is the full-rewrite compaction path).

### The action layer

Every user action is a named `ActionId`. Ribbon, QAT, context menus, chords, tool finder, CLI, and
batch all resolve to one and call `ActionRegistry::dispatch(ActionId, KeyContext)`. **Nothing is wired
directly to a widget slot.** `KeyContext` is computed from the focus chain by `ui/shell/ShortcutBinder`.
This is what makes the same chord mean different things per context (twelve deliberate collisions are
specified in `Features.md` §8) and what makes the headless batch runner and CLI possible without a
second implementation.

## Non-negotiable rules when writing code

These are machine-enforced by four CI gates, not by review discipline:

| Gate | Fails on |
|---|---|
| `tools/layer-lint` | an outward include; `FPDF_` outside `pdf/`; a filesystem write outside `pdf/` and `core/settings/` |
| `tools/mutation-lint` | a `Command` subclass missing from `CommandRegistry`, or lacking a serialize round-trip test or a revert test |
| `tools/i18n-lint` | a user-facing literal not wrapped in `tr()`; a catalog key missing from a shipped locale (`en`, `zh-TW`, `zh-CN`, `ja`) |
| `tools/license-audit` | any dependency SPDX identifier outside the allowlist |

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

## Commands (as specified — nothing builds yet)

```
cmake --preset x64-release
cmake --build --preset x64-release --target checks    # the four gates; CI runs this BEFORE compiling
cmake --build --preset x64-release
ctest --preset x64-release --output-on-failure
```

Presets: `x64-debug`, `x64-release`, `arm64-release`, plus the two non-Windows ones —
`portable-asan`, `portable-fuzz`.

Single test / suite — Catch2 v3 binaries registered with CTest, selected by name regex:

```
ctest --preset x64-debug -R tst_journal --output-on-failure
ctest --preset x64-release -R reversibility     # apply+revert restores the structural hash
ctest --preset x64-release -R roundtrip         # incremental save keeps untouched bytes identical
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

Go/no-go gates ride on Stages 1–3: **A3** (can Qt Widgets host a real ribbon — fallback is a
first-party ribbon widget, *not* a toolkit change), **A2** (p95 per-tile render latency under ~16 ms),
**A4** (does a signature survive a later unrelated incremental save). Stage 0 additionally owes three
toolchain verdicts: **A5** (vcpkg resolves on `x64-mingw-dynamic`) and **A6** (a PDFium DLL links
from `g++`); **A7** (our GCC matches Qt's) is already settled — we build with Qt's own bundled
GCC 13.1.0, and `toolchain-mingw.cmake` keeps it that way. Larger unknowns carried forward: the Write-mode reflow engine
(R1), a first-party full-text index since Xapian is GPL (R2), Office export fidelity (R3), the
sanitizer/PDB capability loss from the GCC toolchain (R7), and the MSVC-shaped corners of the Windows
integration surface — print driver, Office add-ins, arm64 (R8). Open decisions O1–O10 are listed in
`Implementation-Plan.md` §8 — if work forces one, record the resolution there rather than deciding it
inline.
