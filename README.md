# Recto

A Windows-first desktop PDF editor. C++20 · Qt 6.11.1 Widgets · PDFium for read/raster · a
first-party COS layer for every write.

> **Status: Stage 0 in progress — there is no build yet.**
> The design is complete and lives in [`claude-plan/`](./claude-plan/README.md). Source code does not
> exist yet; the commands under *Build* below become live as Stage 0 lands, and each is marked with
> the task that turns it on. This section is written ahead of the code deliberately — the
> clean-machine bootstrap (testing guide case **M-01**) is walked by following it *literally*, so a
> step that is missing here is a defect in the project, not in the tester.

---

## Prerequisites

Exactly two installs. **The Qt installation is the build environment** — CMake, Ninja, the compiler,
`windeployqt`, and the binutils all come from it, so there is no separate toolchain to reconcile.

### 1. Qt 6.11.1 for MinGW

Install with the Qt Maintenance Tool to the default location (`C:\Qt`), selecting:

| Component | Why |
|---|---|
| **Qt 6.11.1 → MinGW 13.1.0 64-bit** | The libraries we link, under LGPL-3.0, dynamically |
| **Developer and Designer Tools → MinGW 13.1.0 64-bit** | **The pinned compiler.** Qt's own bundled GCC |
| **Developer and Designer Tools → CMake** | 3.30.5 |
| **Developer and Designer Tools → Ninja** | 1.12.1 |

**The compiler pin is not a preference.** Qt 6.11.1's MinGW build uses GCC 13.1.0,
`x86_64-w64-mingw32`, SEH, posix threads, and **msvcrt — not UCRT**. An MSYS2 UCRT64 toolchain, or a
newer MinGW-Builds GCC, is a libstdc++/CRT ABI mismatch against this Qt: it links, then crashes
somewhere unrelated weeks later. `cmake/toolchain-mingw.cmake` therefore **fails configure** on
anything else. If you upgrade Qt, the compiler moves in the same change.

> ⚠️ **Do not add Qt Charts or Qt Data Visualization.** Their only open-source license is GPL-3.0,
> and linking either would breach this project's license posture. Having Charts *installed* is
> harmless — `tools/license-audit` fails the build only if something links it.

### 2. vcpkg

```powershell
git clone https://github.com/microsoft/vcpkg "$env:USERPROFILE\vcpkg"
& "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat"
setx VCPKG_ROOT "$env:USERPROFILE\vcpkg"
```

`setx` writes the registry but does **not** reach already-running processes, so a shell open at the
time will not see it. You do not need to restart anything: `scripts\env.ps1` sets `VCPKG_ROOT` itself,
which is what keeps the build from depending on machine state.

Nothing else is required. No Visual Studio, no MSYS2, no Python.

---

## Get the source

```powershell
git clone https://github.com/smolsquirrel936/Recto.git
cd Recto
```

---

## Build

```powershell
.\scripts\bootstrap.ps1          # verify prerequisites; reports what to fix, installs nothing
. .\scripts\env.ps1              # DOT-SOURCE this -- it puts the pinned toolchain on PATH

cmake --preset x64-release                                    # (T3, T4)
cmake --build --preset x64-release --target checks            # (T9) the gates -- always first
cmake --build --preset x64-release                            # (T2)
ctest --preset x64-debug --output-on-failure                  # (T11)
```

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
| `arm64-release` | Windows | **Configures only.** Not validated on hardware |
| `portable-asan` | Linux | `core/` + `pdf/cos/` under ASan+UBSan — MinGW GCC has no sanitizer runtime |
| `portable-fuzz` | Linux | libFuzzer over the COS parser. **No targets until Stage 2** |

The two portable presets are not a portability aspiration: `core/` must build with no display and no
PDF engine linked, and those jobs are what prove it mechanically rather than by assertion.

### Running a single suite

```powershell
ctest --preset x64-debug -R tst_journal --output-on-failure
ctest --preset x64-release -R reversibility     # apply+revert restores the structural hash
ctest --preset x64-release -R roundtrip         # incremental save keeps untouched bytes identical
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
tools/      layer-lint, license-audit, i18n-lint, mutation-lint -- the four gates
tests/      unit, gates, golden, roundtrip, fuzz, bench
corpus/     test PDFs (data)          golden/  reference rasters (data)
i18n/       string catalogs (data)    res/     icons and templates (data)
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
