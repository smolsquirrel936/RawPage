# RawPage

A Windows-first desktop PDF editor. C++20 · Qt 6.11.1 Widgets · PDFium for read/raster · a
first-party COS layer for every write.

> **Status: Stages 0 to 5 are complete and signed off, the last (Stage 5) on 2026-10-04. Stage 6,
> annotations and review, is in progress: T1 … T12 done, the last on 2026-10-14.**
> `rawpage.exe` opens real PDFs into tabs and draws their pages through the pinned PDFium behind our
> own facade, over a first-party COS reader. What a person can do with it today:
>
> - **View and navigate** (Stage 4): zoom, fit, rotate the view, four layout modes, thumbnails,
>   bookmarks and named destinations, rulers, a grid and guides, split views, several windows,
>   full screen and presentation, reading mode, and a session restored at the next launch.
> - **Edit and save** (Stage 3): every change is a journalled command, undone and redone with
>   `Ctrl+Z`/`Ctrl+Y`, saved incrementally through our own writer and recovered after a crash.
> - **Read the text** (Stage 5): select and copy, a find bar, Advanced Search over a folder, a
>   Snapshot, Reflow view, and a side-by-side or overlaid document comparison.
> - **Annotate** (Stage 6, so far): highlight, underline, strikeout and squiggly; replace and
>   insert text; notes; text boxes, callouts and typewriter text; lines, arrows, shapes, clouds,
>   ink and the eraser; a Comments pane with replies, statuses and checkmarks; stamps (standard,
>   dynamic, image and custom); attachments -- files attached to the document or pinned to a
>   page, listed in an Attachments pane, saved out, or opened (a PDF in a tab of its own; a program
>   never); and measurement -- distance, perimeter, area and dimension callouts at a scale set from
>   two lengths or calibrated from one, each point snapping to the drawing's corners, midpoints and
>   crossings. Each one is drawn by our own appearance builder and saved as written.
>
> **Not built yet:** FDF/XFDF exchange and audio comments (the rest of
> Stage 6), then document assembly, export and optimization, forms, security and redaction,
> signatures, creation and import, Write mode, batch and the CLI, packaging and release (Stages 7 to
> 15), and the post-v1 subsystems (Stage 16).
> **This section carries no counts on purpose.** Live status is [`CLAUDE.md`](./CLAUDE.md) →
> *Where the work is right now* and each stage's block in
> [`Implementation-Plan.md`](./claude-plan/Implementation-Plan.md) §4. The design lives in
> [`claude-plan/`](./claude-plan/README.md).
>
> This section is walked literally by Stage 0's clean-machine case **M-01**
> (`claude-plan/Stage0-Toolchain-Testing-Guide.md` §6) on a clean machine, so a step that
> is missing here is a defect in the project, not in the tester.

---

## Prerequisites

Three installs, one of which is git. **The Qt installation is the build environment** — CMake,
Ninja, the compiler, `windeployqt`, and the binutils all come from it, so there is no separate
toolchain to reconcile.

### 1. Qt 6.11.1 for MinGW

Install with the Qt Maintenance Tool to the default location (`C:\Qt`), selecting:

| Component | Why |
|---|---|
| **Qt 6.11.1 → MinGW 13.1.0 64-bit** | The libraries we link, under LGPL-3.0, dynamically |
| **Developer and Designer Tools → MinGW 13.1.0 64-bit** | **The pinned compiler.** Qt's own bundled GCC |
| **Developer and Designer Tools → CMake** | 3.30.5 |
| **Developer and Designer Tools → Ninja** | 1.12.1 |

> **6.11.1 is not offered by default.** The online installer lists only the newest patch of each
> minor series, which today means 6.11.2. In **Select Components**, tick **Archive** in the category
> filter and press **Filter** — 6.11.1 then appears in the tree. Installing 6.11.2 instead gives a
> configure failure from TA-8, not a build.
>
> The two **MinGW 13.1.0 64-bit** rows above are different components with the same name: the one
> under *Qt 6.11.1* is the libraries, the one under *Developer and Designer Tools* is the compiler at
> `C:\Qt\Tools\mingw1310_64`. You need both.

**The compiler pin is not a preference.** Qt 6.11.1's MinGW build uses GCC 13.1.0,
`x86_64-w64-mingw32`, SEH, posix threads, and **msvcrt — not UCRT**. An MSYS2 UCRT64 toolchain, or a
newer MinGW-Builds GCC, is a libstdc++/CRT ABI mismatch against this Qt: it links, then crashes
somewhere unrelated weeks later. `cmake/toolchain-mingw.cmake` therefore **fails configure** on
anything else. If you upgrade Qt, the compiler moves in the same change.

> ⚠️ **Take nothing from Additional Libraries.** Qt Charts, Qt Data Visualization, and their
> successor Qt Graphs are GPL-3.0-only, and linking any of them would breach this project's license
> posture. Having them *installed* is harmless — `tools/license-audit` fails the build only if
> something links one — but a lean box is what makes the clean-machine case (M-01) worth running.

### 2. vcpkg

```powershell
git clone https://github.com/microsoft/vcpkg "$env:USERPROFILE\vcpkg"
& "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat"
setx VCPKG_ROOT "$env:USERPROFILE\vcpkg"
```

`setx` writes the registry but does **not** reach already-running processes, so a shell open at the
time will not see it. You do not need to restart anything: `scripts\env.ps1` sets `VCPKG_ROOT` itself,
which is what keeps the build from depending on machine state.

### 3. Git for Windows

Needed to clone the repository, and `scripts\bootstrap.ps1` checks for it. Any recent version;
nothing about it is pinned.

Nothing else is required to build. No Visual Studio, no MSYS2, no Python — Python is a
verification dependency only, for the corpus and coverage checks the `gates` preset runs.

---

## Get the source

```powershell
git clone https://github.com/smolsquirrel936/RawPage.git
cd RawPage
```

---

## Build

```powershell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned   # once per account

.\scripts\bootstrap.ps1          # verify prerequisites; reports what to fix, installs nothing
. .\scripts\env.ps1              # DOT-SOURCE this -- it puts the pinned toolchain on PATH

cmake --preset x64-release
cmake --build --preset x64-release --target checks            # the gates -- always first
cmake --build --preset x64-release
ctest --preset x64-release --output-on-failure                # 1745/1745 at Stage 6 T12, 2026-10-14
```

A stock Windows install runs no scripts at all: without the first line, both `.ps1` files fail with
*"running scripts is disabled on this system"*. `RemoteSigned` runs local unsigned scripts and still
blocks unsigned downloaded ones; a `git clone` does not set the mark-of-the-web, so the repository's
scripts count as local. `-Scope Process -ExecutionPolicy Bypass` is the session-only alternative.

Each preset owns a separate tree under `build/<preset>`, so `ctest --preset x64-debug` needs
`x64-debug` configured and built first — it is not a debug view of the release build.

`checks` runs the enforcement gates **before** anything is compiled, so a layering or license
violation fails in seconds rather than after a full build. It is not optional and not a lint pass you
run when convenient — it is the mechanism the architecture is held together by.

`env.ps1` must be **dot-sourced** (`. .\scripts\env.ps1`). Running it directly modifies a child
process that immediately exits; the script detects this and tells you.

### Presets

| Preset | Platform | Purpose |
|---|---|---|
| `x64-debug` | Windows | Day-to-day development and `ctest` |
| `x64-release` | Windows | Optimized build, the release configuration |
| `gates` | any | **tools/ and tests/gates/ only — no Qt, no vcpkg.** What CI runs first; ~4 s |
| `arm64-release` | Windows | **Placeholder — does not configure.** Fails with an explanation: the pin is x86_64 GCC 13.1.0, and whether arm64 means moving to LLVM-MinGW is open decision O6 |
| `portable-asan` | Linux | `core/` + `pdf/cos/` + `pdf/ops/` under ASan+UBSan — MinGW GCC has no sanitizer runtime |
| `portable-fuzz` | Linux | libFuzzer over five targets: the COS lexer and parser, the reader-writer round trip, the text pipeline and the annotation pipeline. CI only; `.github/workflows/fuzz-soak.yml` is the daily ten-minute soak |

The two portable presets are not a portability aspiration: `core/` must build with no display and no
PDF engine linked, and those jobs are what prove it mechanically rather than by assertion.

### Running a single suite

Catch2 registers each case with CTest **by its name**, prefixed `unit.`, not by its tag — so `-R`
matches the sentence, not the file. Test names are ASCII by convention: a non-ASCII character does not
survive CTest's filter round trip and the case then "fails" with a passing body.

```powershell
ctest --preset x64-debug -R "distinct error codes" --output-on-failure
ctest --preset x64-debug -R "thirteen Write-mode collisions" --output-on-failure

ctest --preset x64-release -R reversibility     # apply+revert restores the structural hash (Stage 3)
ctest --preset x64-release -R roundtrip         # incremental save keeps untouched bytes (Stage 3)
```

---

## Repository layout

```
app/        composition root -- the only code that names a concrete backend or platform
ui/         Qt Widgets. Never includes pdf/ or platform/win/, never mutates a document
core/       the domain. Builds and tests headless: no display, no PDF engine
pdf/        the only layer that may emit PDF bytes or call FPDF_*
platform/   platform.h interfaces + win/, the only OS-specific code
cmake/      the pinned toolchain and shared build modules
tools/      the four gates: layer-lint, license-audit, i18n-lint, mutation-lint
            ribbon-validate -- res/ribbon.json's build-time check. NOT a gate: `checks`
            runs four and says four; this one is a build step ui/ depends on
            golden-update -- the only producer of golden/; cjk-coverage -- a checked generator
tests/      unit, platform, ui, render, golden, composition, roundtrip, gates, fuzz, bench
            (Implementation-Plan.md §2.1 says why each test target is separate)
            a bench REPORTS a figure -- its exit code says whether the measurement was
            possible, never whether the machine was fast
corpus/     test PDFs (data)          golden/  reference rasters (data)
i18n/       string catalogs (data)    res/     ribbon.json (data); icons when they exist
```

**Dependencies point inward only**, and `tools/layer-lint` fails the build on any edge that does not.
Before writing code in a layer, read
[`Architecture-FileMap.md` §1](./claude-plan/Architecture-FileMap.md) for what that layer may include.

---

## Contributing

Read [`claude-plan/README.md`](./claude-plan/README.md) first — it is the index to the design set and
says which document owns which facts. The rules that are machine-enforced, not review-enforced:

- Every document change is a `Command` run through `CommandJournal`. A dialog that contains document
  logic is a rejection, not a style note.
- **A new `Command` ships with its inverse and its tests, or it does not ship.**
- No feature that the headless batch path cannot reach.
- Every settings key is declared once in `SettingsSchema`; an undeclared key is a compile error.
- `Result<T>` at boundaries — no exception crosses a layer.
- No GPL or AGPL dependency, ever. The one named exception is the GCC runtime under its Runtime
  Library Exception, which is why `license-audit` matches whole SPDX expressions and never a `GPL`
  substring.

---

## Third-party notices

`THIRD-PARTY.md` is **generated** by `tools/license-audit` from the resolved dependency set — never
hand-edited, and committed per release. Qt is used under **LGPL-3.0**, linked dynamically and shipped
as replaceable DLLs beside the executable; never statically linked.
