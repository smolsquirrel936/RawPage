# RawPage

A Windows-first desktop PDF editor. C++20 · Qt 6.11.1 Widgets · PDFium for read/raster · a
first-party COS layer for every write.

> **Status: Stage 0 complete; Stage 1 in progress — tasks T1–T10 of 15 done (2026-08-24).**
> The commands below work. What they build is still mostly *scaffolding*: a pinned toolchain, three
> enforcement gates, the domain vocabulary, the entry point with its crash handler, the action layer
> (`ActionId`, `ActionRegistry`, `KeyBindingStore`), the typed settings store, the `en`/`zh-TW`
> string catalogs, the OS boundary with its single-instance bus, and the composition root that wires
> all of it together. **`rawpage.exe` now opens a window with a working ribbon** — 9 tabs, 49 groups
> and 192 controls, every one built from `res/ribbon.json` and dispatched through the action registry,
> above a sidebar, a status bar and a live theme. Most of those controls are **disabled and say why**:
> Stage 1 declares 358 commands and registers the four this window can actually service. **No PDF can
> be opened** until Stage 2.
> Risk **A3** (can Qt Widgets host a real ribbon without a commercial control library?) is settled:
> **go, with named custom work.**
> The design lives in [`claude-plan/`](./claude-plan/README.md); live status is the Stage 1 block in
> [`Implementation-Plan.md`](./claude-plan/Implementation-Plan.md).
>
> This section is walked literally by testing-guide case **M-01** on a clean machine, so a step that
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

Nothing else is required. No Visual Studio, no MSYS2, no Python.

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
ctest --preset x64-release --output-on-failure                # 250 tests
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
| `portable-asan` | Linux | `core/` + `pdf/cos/` under ASan+UBSan — MinGW GCC has no sanitizer runtime |
| `portable-fuzz` | Linux | libFuzzer over the COS parser. **No targets until Stage 2** |

The two portable presets are not a portability aspiration: `core/` must build with no display and no
PDF engine linked, and those jobs are what prove it mechanically rather than by assertion.

### Running a single suite

Catch2 registers each case with CTest **by its name**, prefixed `unit.`, not by its tag — so `-R`
matches the sentence, not the file. Test names are ASCII by convention: a non-ASCII character does not
survive CTest's filter round trip and the case then "fails" with a passing body.

```powershell
ctest --preset x64-debug -R "distinct error codes" --output-on-failure
ctest --preset x64-debug -R "twelve Write-mode collisions" --output-on-failure

# Suites that do not exist yet, named here so the shape is on record:
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
tools/      layer-lint + license-audit + i18n-lint (live); mutation-lint (Stage 3)
            ribbon-validate -- res/ribbon.json's build-time check. NOT a gate: `checks`
            runs three and says three; this one is a build step ui/ depends on
tests/      unit, gates, ui, platform, golden, roundtrip, fuzz, bench
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
