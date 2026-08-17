# Fixture: `wrong-compiler`

Negative tests for the toolchain pin in `cmake/ToolchainAssert.cmake`. Each one must make
**configure fail** with its own named diagnostic — a gate that fails for an unrelated reason has not
been tested.

Written at **T3**, ahead of its home task **T9**, because TA-2 and TA-6 are the two assertions doing
the most work and leaving them verified in only the positive direction was the weakest point in the
stage. T9 wraps these in CTest cases (`gates.wrong_compiler.*`); until then they are run by hand.

## Why these are macro overrides and not a second toolchain

The realistic regression is a developer whose MSYS2 UCRT64 install wins a `PATH` race. Reproducing
that faithfully needs a second, genuinely different GCC on the machine, which no fixture can conjure.

What these fixtures do instead is drive the compiler to *report* the wrong thing, which exercises the
assertion's logic and its message. That is a real test of the gate and **not** a test of the scenario.
The scenario stays with manual case **M-02**, which requires a real UCRT64 install.

Both matter. If the logic is wrong, M-02 fails confusingly on someone's machine months from now; if
only the logic is tested, the pin has never met a real intruder.

## Cases

Run from the repository root, with `. .\scripts\env.ps1` dot-sourced first. Every case **passes when
`cmake` exits non-zero** and the quoted text appears.

```powershell
$tc = "-DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-mingw.cmake" -replace '\\','/'
```

### WC-1 · TA-2, wrong compiler version

```powershell
cmake -S . -B build/wc1 -G Ninja $tc "-DCMAKE_CXX_FLAGS=-U__GNUC_MINOR__ -D__GNUC_MINOR__=9"
```

CMake's compiler-identification step reads `__GNUC__`, `__GNUC_MINOR__`, and `__GNUC_PATCHLEVEL__`
from a probe compiled with `CMAKE_CXX_FLAGS`, so overriding one of them moves the reported version.

**Expect** exit ≠ 0 · `Toolchain pin violated: TA-2 compiler version` · `found: 13.9.0` ·
`expected: 13.1.0`.

### WC-2 · TA-6, UCRT runtime

```powershell
cmake -S . -B build/wc2 -G Ninja $tc "-DCMAKE_CXX_FLAGS=-D_UCRT"
```

**Expect** exit ≠ 0 · `Toolchain pin violated: TA-6 C runtime` · `found: UCRT (_UCRT is defined)` ·
and a message naming MSYS2 ucrt64 as the usual source.

> This case is the reason TA-6's probe is compiled **with** `CMAKE_CXX_FLAGS`. An earlier version ran
> the bare compiler, so this exact command configured cleanly and reported "msvcrt" while the entire
> build compiled as UCRT — the assertion was interrogating a compilation the project never performs.

### WC-3 · TA-6, probe self-check

```powershell
cmake -S . -B build/wc3 -G Ninja $tc "-DCMAKE_CXX_FLAGS=-U__MSVCRT__"
```

TA-6 requires `__MSVCRT__` to be **present**, not merely `_UCRT` to be absent. Testing only for the
absence would pass vacuously whenever the macro dump failed, and a check that passes when its own
probe breaks is not a check. This case is what keeps that property honest.

**Expect** exit ≠ 0 · `found: __MSVCRT__ is not defined`.

### WC-4 · Toolchain root points nowhere

```powershell
cmake -S . -B build/wc4 -G Ninja $tc "-DRECTO_MINGW_ROOT=C:/nonexistent-gcc"
```

**Expect** exit ≠ 0 · `No g++.exe under RECTO_MINGW_ROOT=` · and the Qt Maintenance Tool fix.

### WC-5 · GCC plugin injected

```powershell
cmake -S . -B build/wc5 -G Ninja $tc "-DCMAKE_CXX_FLAGS=-fplugin=evil.dll"
```

**Expect** exit ≠ 0 · `loads a GCC plugin` · and the Runtime Library Exception consequence.

> Caught by the scan in `toolchain-mingw.cmake`, not by TA-9. A plugin that fails to load breaks
> GCC's own compiler probe before `project()` returns, and CMake then reports only "the C++ compiler
> is not able to compile a simple test program". TA-9 still owns the case that matters more: a plugin
> that **does** load and so never trips the probe.

### WC-6 · Static runtime requested

```powershell
cmake -S . -B build/wc6 -G Ninja $tc "-DCMAKE_CXX_FLAGS=-static-libstdc++"
```

**Expect** exit ≠ 0 · `contains a -static* flag` · citing the LGPL obligation in
`Tech-Stack-Plan.md` §8.1.

## Not covered here

- **TA-1** (non-GNU compiler) — needs a second compiler; no cheap simulation.
- **TA-3, TA-4, TA-5, TA-7** — each needs a differently *built* GCC. A `-target` flag does not move
  `-dumpmachine`, and the SEH/thread-model checks read files on disk.
- **TA-8** (Qt version/prefix) — needs a second Qt install.

These are covered by inspection and by M-02, and are recorded here so their absence is a known state
rather than an oversight.
