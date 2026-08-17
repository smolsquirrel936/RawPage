# PDFium (prebuilt)

**Nothing in this directory is committed except this file.** Run `scripts\fetch-pdfium.ps1`
to populate it; the script verifies a SHA-256 before it will use the archive.

| | |
|---|---|
| Version | **153.0.8009.0** (release tag `chromium/8009`) |
| Asset | `pdfium-win-x64.tgz` — **not** the `-v8-` variant |
| SHA-256 | `c78a8cd51b48abafcb266d868e401afefda1d189aa01ebbe743dc1d144e06031` |
| Upstream | https://github.com/bblanchon/pdfium-binaries |
| PDFium licence | **BSD-3-Clause** |

## Why prebuilt, and why this variant

There is no vcpkg port for PDFium, and building it from source needs `depot_tools`, GN, and
its own clang — days of work and a very large checkout. Risk **A6** only ever asked whether
a PDFium **DLL** can be consumed from a `g++` build, and a prebuilt binary answers that
directly. Building from source remains the documented fallback if a DLL cannot be obtained.

The non-V8 variant is pinned deliberately: Recto uses PDFium for **read and raster only**, so
the V8 JavaScript engine would add ~9 MB and a large attack surface for an entry point we
never call.

## The import library is generated, not shipped

Upstream ships `lib/pdfium.dll.lib`, which is MSVC format. `fetch-pdfium.ps1` runs `gendef`
over the DLL's export table (472 symbols) and `dlltool` to produce `lib/libpdfium.dll.a` for
GNU ld. Both tools come from `C:\Qt\Tools\mingw1310_64\bin`, which is why Tech-Stack-Plan.md
§6.3 lists them as load-bearing rather than incidental.

## Two findings worth knowing before Stage 2

**PDFium statically links its own C runtime.** Measured 2026-08-18: `pdfium.dll` imports only
`KERNEL32`, `ADVAPI32`, `GDI32` and `USER32` — no `msvcrt.dll`, no `ucrtbase.dll`. So there is
no shared-CRT question with our binary at all, which is a *stronger* result than A6 assumed.

It also makes one facade rule non-negotiable rather than stylistic: **memory allocated by
PDFium must be released by PDFium**, through the matching `FPDF_*` call. Our `free()` would
be handing a pointer to a different allocator's heap. The `pdf/render/` facade already passes
nothing but buffers and callbacks; this is the concrete reason why.

**The archive's `LICENSE` is the packager's, not PDFium's.** It is Benoit Blanchon's MIT
licence covering the build scripts. PDFium's own BSD-3-Clause text and its third-party
notices are *not* in the archive, and Tech-Stack-Plan.md §8.1 requires us to retain and ship
them. Fetching those into `res/licenses/` is owed before release — Stage 15's release gate.
