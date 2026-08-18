# `fake-root/` — a compiler that exists but cannot run

`bin/g++.exe` is a text file. That is the entire point: it satisfies the
`EXISTS "${RECTO_MINGW_ROOT}/bin/g++.exe"` guard and then fails to start, which is the state
`recto_preflight_compiler_runs()` was added to explain (**I-10**).

The real-world shape of this is a mingw `g++` whose own `bin` directory is off `PATH`: the
driver spawns `cc1plus.exe` from `lib/gcc/<triple>/<version>/`, which is not the driver's
directory, so the DLLs beside `g++.exe` are never found. M-02 hit exactly that with a real
MSYS2 UCRT64 install, and CMake reported only *"the C++ compiler is not able to compile a
simple test program"* — true, and nowhere near the reason.

Using a real second toolchain here would make the case depend on what happens to be installed
on the machine. A text file is deterministic and needs nothing.
