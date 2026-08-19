# Recto — Tech Stack, Architecture & Mutation Foundation Plan

## 1. Context

**Recto** is the Windows-first desktop PDF editor whose full capability surface is catalogued in
[`Features.md`](./Features.md) — view/find, create/export, dual-mode editing, review & annotation,
assembly, forms, and security & signing. This document locks the decisions that are **expensive to
reverse** and must therefore be made before any code exists. It answers five specific questions:

1. **What is it built with?** — language, UI toolkit, PDF engine, build system.
2. **What is the license posture?** — what obligations we take on, and what we must never do.
3. **What is the layer shape?** — the directory layout and the one dependency direction.
4. **How does the document change?** — the single cross-cutting concern that shapes everything:
   *every mutation of a PDF must be a reversible, journalled command, and no UI or core code may
   touch the PDF's bytes directly.* Getting this wrong is unrecoverable at scale; a PDF editor that
   grows ad-hoc mutation paths cannot deliver reliable undo, incremental save, crash recovery, or
   signature-integrity guarantees.
5. **What do we build first?** — the thin vertical slice that retires the most architectural risk.

It deliberately does **not** produce a file-by-file map (that is
[`Architecture-FileMap.md`](./Architecture-FileMap.md)) or a build sequence (that is
[`Implementation-Plan.md`](./Implementation-Plan.md)).

---

## 2. Decisions (confirmed)

| # | Decision | Status |
|---|---|---|
| D1 | **Language:** C++20, **MinGW-w64 GCC 13.1.0** — the compiler Qt ships in `C:\Qt\Tools\mingw1310_64`, `x86_64-w64-mingw32`, **SEH** exceptions, **posix** threads, **msvcrt**-based. `-std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor`, x64 first, arm64 as a second target (see R8). Pinned exactly, not "13.x or newer" — §6.3. | ✅ Confirmed |
| D2 | **UI toolkit:** **Qt 6.11.1, Widgets** (not QML), **dynamically linked** under **LGPL-3.0**, from the official binary install at `C:\Qt\6.11.1\mingw_64`. | ✅ Confirmed |
| D3 | **PDF engine:** **PDFium** (BSD-3-Clause), used for parsing, rasterization, text extraction, and form-field rendering — wrapped behind our own facade, **plus a first-party COS object/write layer** for the mutations PDFium does not expose. | ✅ Confirmed |
| D4 | **License posture:** Recto's own source is **proprietary and closed**. This is compatible only because Qt is dynamically linked under LGPL-3.0 and every other dependency is permissive (BSD/MIT/Apache-2.0). **No GPL or AGPL dependency may ever enter the tree** — this specifically rules out MuPDF, Ghostscript, Poppler-with-GPL-linkage, and Xpdf. | ✅ Confirmed |
| D5 | **Headline cross-cutting design:** **mutation-through-commands** — every change to a document is a `Command` object applied to `core::Document` through `core::CommandJournal`; nothing else may mutate a document, and no `ui/` or `core/` code may call the PDF engine's write APIs. Persistence is by **incremental save** (append a new cross-reference section) with full-rewrite Save-As as the compaction path. | ✅ Confirmed |
| D6 | **Secondary locked convention:** **strings-out-of-code** — no user-facing literal in source; all UI text goes through Qt's `tr()` into `.ts`/`.qm` catalogs, shipping `en`, `zh-TW`, `zh-CN`, and `ja` from the first release. Enforced by a CI lint, not by discipline. | ✅ Confirmed |
| D7 | **Build:** CMake ≥ 3.25 with presets, **vcpkg in manifest mode** (triplet `x64-mingw-dynamic`) for third-party dependencies, Ninja generator. Qt itself comes from its **official Windows MinGW binary build**, not from vcpkg. The Qt installation supplies the whole toolchain — compiler, CMake 3.30.5, Ninja 1.12.1, `windeployqt` — so a build machine needs nothing else. | ✅ Confirmed |
| D8 | **Packaging:** MSI (WiX v4) for administrative deployment, plus a per-user MSIX. Qt libraries deployed via `windeployqt` as **replaceable DLLs beside the executable** — never statically linked, never bundled in a way that prevents relinking. The **GCC runtime DLLs** (`libstdc++-6`, `libgcc_s_seh-1`, `libwinpthread-1`) ship beside them, also dynamically. | ✅ Confirmed |
| D9 | **Testing baseline:** Catch2 v3 for unit tests, a first-party golden-image harness for rendering, a corpus round-trip harness for save fidelity, and libFuzzer over the COS parser. **GCC has no libFuzzer and MinGW has no sanitizer runtime**, so the sanitizer and fuzz gates run on a portable non-Windows CI job over `core/` and `pdf/cos/` — §6.3, R7. | ✅ Confirmed |
| D10 | **Rendering model:** page content is rasterized **asynchronously into tiles** on a worker pool at the target monitor's physical DPI; the UI thread never parses or rasterizes. | ✅ Confirmed |

---

## 3. Evidence informing the choices

### 3.1 What the reference product tells us (documented, verifiable)

Right PDF Professional's own user manual ([S1, S2 in `Features.md`](./Features.md#sources)) is
strong evidence about the *shape* a shipping peer in this category needs, independent of its
implementation:

| Observation from the manual | What it implies for our stack |
|---|---|
| The product ships a **virtual PDF printer**, **Office add-ins** for five applications, **browser extensions**, **scanner acquisition**, **Windows certificate-store signing**, and **Azure Information Protection** support. | The OS-specific surface is large and unavoidable, but it is all *peripheral* — none of it is in the document-editing path. This is exactly the case for a thin, wide platform abstraction (§4.3) rather than a deep one. |
| It has a **dual-mode editing model** — a fixed-layout Review mode and a reflowable Write mode reached via *Convert to Writable*, with a one-way *Prevent Further Edits* finalization. | Two genuinely different document representations coexist. Our architecture must treat "the reflowable rendition" as a **separate model** with an explicit conversion at each boundary, not as a mode flag on one model. This directly shapes `core/` (§4.2). |
| It has a separate **Server** edition and an **SDK**. | Their engine is clearly usable headlessly, i.e. separated from their UI. We take the same constraint as a hard rule: `core/` and `pdf/` must build and run with no UI and no display. |
| It ships **batch processing**, a **command-line converter**, and **watched folders**. | Every document operation must be invocable without a window. A command/action layer that the UI merely *drives* is therefore not a nicety; it is required by the feature set. |
| It supports **OCR, portfolios, 3D (U3D/PRC), multimedia, and full-text catalog indexing**. | These are large, separable subsystems. They must be able to arrive late without reshaping the core — which argues for interface-mediated subsystems (`IOcrEngine`, `IIndexer`) declared early even when unimplemented. |

### 3.2 Why PDFium, specifically

| Candidate | License | Verdict |
|---|---|---|
| **PDFium** | BSD-3-Clause | **Chosen.** Permissive, so it imposes nothing on our source. It is the engine inside Chrome and Edge, which means its parser has absorbed more adversarial real-world input than any alternative — decisive for a product that must open damaged files without crashing. It gives us parsing, rasterization, text extraction with positions, and AcroForm rendering. |
| MuPDF | AGPL-3.0 or commercial | **Rejected on license (D4).** Excellent engine and a cleaner API, but AGPL would force Recto's own source open. The commercial license is a viable alternative only if we ever revisit D4. |
| Poppler | GPL-2.0/3.0 | **Rejected on license (D4).** |
| Foxit / Apryse SDK | Commercial, per-seat royalty | **Rejected for now.** Would deliver far more out of the box (OCR, conversion, signing) but binds the architecture to a vendor object model and adds a recurring per-seat cost to every copy sold. Kept as a documented fallback for a specific subsystem (see §9 open risk R3). |
| Write our own parser | — | **Rejected as the primary engine.** Years before matching a browser engine's tolerance for malformed files. |

**Where PDFium is not enough, and what we do about it.** PDFium is a *viewer* engine. Its public
API can render, extract text, fill forms, and do coarse page manipulation, but it does **not** offer
what an editor needs: byte-preserving incremental save, arbitrary indirect-object editing,
annotation appearance-stream authoring, redaction of content-stream operators, encryption
re-keying, or cross-reference repair with a report. Therefore **D3 splits the engine in two**:

- `pdf/render/` — a thin facade over PDFium for **read and raster** only.
- `pdf/cos/` — our **own** COS (Carousel Object System) layer: lexer, object model
  (`CosDict`/`CosArray`/`CosStream`/`CosRef`), cross-reference table and stream parsing, filters,
  encryption, and an **incremental writer**. This is the part we own outright, and the part the
  fuzzer targets.

This split is the single most consequential technical decision in the document, so it is called out
explicitly: **we read through PDFium and write through our own code.** Both are hidden behind
interfaces that `core/` declares, so either half can be replaced.

### 3.3 Assumptions that are NOT yet verified

The following are *inferences*, not measurements. They are recorded here so they are not later
mistaken for evidence, and each has a concrete way to settle it. **Stage 0 owes this verification.**

| # | Unverified assumption | How to settle it |
|---|---|---|
| A1 | Right PDF's Write mode is a first-party reflow engine rather than an embedded third-party word processor. | Inspect an installed copy's binaries and dependency list; check for a known word-processing engine's DLLs. |
| A2 | PDFium's `FPDF_RenderPageBitmap` throughput is sufficient for smooth 60 Hz scrolling of a text-heavy A4 page at 200% on a 150 %-scaled 4K monitor with an 8-worker pool. | Build the Stage 2 spike and measure: per-tile milliseconds at the target DPI, p50/p95, over a 20-file corpus. **This is a go/no-go for D3's render half.** |
| A3 | Qt Widgets can deliver a ribbon, tear-off document tabs, and dockable auto-hiding panes without a commercial ribbon library. | Prototype the shell in Stage 1. If it cannot, the fallback is a first-party ribbon widget — not a switch away from Qt. |
| A4 | Incremental save can preserve the original bytes closely enough that a signature applied before an unrelated later edit still validates. | Corpus round-trip harness in Stage 3: sign, then add an annotation incrementally, then validate. |
| ~~A5~~ | vcpkg has serviceable ports for every dependency in §6 at the versions we want, **on the `x64-mingw-dynamic` triplet**. | ✅ **Resolution settled 2026-08-18 — see below.** Build fidelity remains partly open. |
| ~~A6~~ | **PDFium can be consumed from a GCC-built binary.** ✅ **Settled 2026-08-18 — see below.** ~~ The `FPDF_*` surface is a **C** API and x64 Windows has a single calling convention, so a PDFium **DLL** — however it was compiled — is linkable from `g++`; only C++ objects, exceptions, and CRT handles crossing the boundary would break, and the facade in `pdf/render/` passes none of them.~~ | Stage 0: link a trivial `FPDF_InitLibrary`/`FPDF_LoadDocument` program against a prebuilt `pdfium.dll` (generating a `.dll.a` with `gendef` + `dlltool` if no import library is shipped) and open a corpus file. **If a DLL build cannot be obtained or built, the fallback is to build PDFium from source with its own clang toolchain and emit a DLL — not to switch the engine.** A static MSVC `pdfium.lib` is *not* an option. |
| ~~A7~~ | **Our GCC and Qt's GCC are ABI-compatible.** | ✅ **Settled 2026-08-17 — see below.** |

**A6, measured on the build machine (2026-08-18), Stage 0 task T12.** A prebuilt PDFium DLL
links from `g++` and works. Pinned: **PDFium 153.0.8009.0**, `pdfium-win-x64.tgz` from release
`chromium/8009`, SHA-256 `c78a8cd51b48ab…`, acquired by `scripts/fetch-pdfium.ps1`.

| Step | Result |
|---|---|
| `gendef` over the DLL export table | 472 symbols |
| `dlltool` → `libpdfium.dll.a` | 379 KB GNU import library (upstream ships MSVC-format `.lib` only) |
| `g++ -std=c++20 … -lpdfium` | links, no warnings |
| `FPDF_InitLibraryWithConfig` → `FPDF_LoadDocument` → `FPDF_GetPageCount` | `pages=1`, `page0=ok 595.276 x 841.890 pt` |
| `objdump -p spike.exe` | `KERNEL32.dll`, **`msvcrt.dll`**, `pdfium.dll` — **no `ucrtbase.dll`** |

**The result is stronger than the assumption.** A6 worried about a C runtime arriving with
PDFium and mixing with ours. It cannot: `pdfium.dll` imports **no CRT at all** — only
`KERNEL32`, `ADVAPI32`, `GDI32`, `USER32` — because it statically links its own. There is
therefore no shared-CRT surface between us and PDFium in either direction.

That has one consequence which is now a rule rather than a preference: **memory allocated by
PDFium must be released by PDFium**, through the matching `FPDF_*` call. Calling our `free()`
on a PDFium pointer hands it to a different allocator's heap. The `pdf/render/` facade already
passes nothing but buffers and callbacks; this measurement is the concrete reason why.

**One obligation found and carried forward.** The prebuilt archive's `LICENSE` is the
*packager's* MIT licence covering the build scripts — **PDFium's own BSD-3-Clause text and its
third-party notices are not in it**. §8.1 requires retaining and shipping them, so fetching
those into `res/licenses/` is owed by Stage 15's release gate.

**A5, measured on the build machine (2026-08-18), Stage 0 task T6.** Every §6.2 dependency that comes
from vcpkg **resolves** on `x64-mingw-dynamic` at the pinned baseline `94a5411977…`. No port is
MSVC-only, and **no vendored overlay is needed** — `tools/ports/` stays empty, which was the outcome
this assumption existed to check.

| Feature group | Resolved versions |
|---|---|
| `tests` (default) | catch2 3.15.3 |
| `codecs` | zlib 1.3.2#2 · libjpeg-turbo 3.2.0 · libpng 1.6.58 · openjpeg 2.5.4 · tiff 4.7.2 |
| `text` | freetype 2.14.3 · harfbuzz 14.3.1 · icu 78.3#2 · hunspell 1.7.3 |
| `crypto` | openssl 3.6.3 |
| `ocr` | leptonica 1.87.0 · tesseract 5.5.2 |

**What is proven, and what is not.** Resolution is not compilation. `--dry-run` proves the dependency
graph closes and a version exists for the triplet; only the default set was actually **built**
(catch2 → `libCatch2.a`, installed and consumed through the preset). ICU and OpenSSL are the two with
a reputation for failing under MinGW, and neither has been compiled here. Each remaining group is
built when its stage switches the feature on — Stage 2 for `codecs`, Stage 5 for `text`, Stage 10 for
`crypto`, Stage 16 for `ocr` — and **that** is when A5 is fully closed for it. Recording this as a
flat pass would be the overclaim A5 exists to prevent.

Two operational facts fell out of the measurement and are load-bearing for CI:

- vcpkg's `x64-mingw-dynamic` triplet runs a `detect_compiler` probe that invokes the pinned `g++`.
  **Without `C:\Qt\Tools\mingw1310_64\bin` on `PATH`, that probe fails** (`cc1plus` cannot load its
  own DLLs, `0xC0000135`) and vcpkg reports *"unable to detect the active compiler's information"* —
  indistinguishable from a broken triplet. This produced a false-negative A5 reading before the cause
  was found.
- **`VCPKG_HOST_TRIPLET` must be set to `x64-mingw-dynamic`.** At its default, vcpkg builds host tools
  for `x64-windows` and resolves MSVC — present on the current dev machine, absent by design on the
  clean machine of manual case M-01.

vcpkg also emits, for every port: *"community triplets are not built in the curated registry and are
thus less likely to succeed"*. That is the risk this assumption named, stated by the tool itself.

**A7, settled by inspection of the installed toolchain (2026-08-17).** The question dissolves rather
than being answered: **we build with the compiler Qt ships**, so there is no second toolchain to
match. Measured on the build machine:

| Fact | Value | How it was established |
|---|---|---|
| Qt | **6.11.1**, `C:\Qt\6.11.1\mingw_64`, qmake spec `win32-g++` | `qmake -query QT_VERSION` |
| Compiler | **GCC 13.1.0**, `C:\Qt\Tools\mingw1310_64` | `g++ --version` |
| Target triple | `x86_64-w64-mingw32` | `g++ -dumpmachine` |
| Exception / thread model | **SEH** / **posix** | `g++ -v` banner (`x86_64-posix-seh-rev1`) |
| **CRT flavour** | **msvcrt**, *not* UCRT | GCC's configure sysroot `…-posix-seh-msvcrt-rt_v11-rev1`, corroborated by `Qt6Core.dll` importing `msvcrt.dll` |
| Runtime DLLs Qt itself imports | `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll` | `objdump -p Qt6Core.dll` |

The msvcrt row is the one that bites: **an MSYS2 UCRT64 toolchain is not a substitute**, however
current its GCC. Same for any newer MinGW-Builds release — a different libstdc++ against this Qt is
the week-long crash hunt this pin exists to prevent. If Qt is ever upgraded, the compiler moves with
it, in the same change.

---

## 4. Target architecture

### 4.1 Directory layout

```
recto/
  app/                   # COMPOSITION ROOT — the only layer that knows concretes exist
                         #   main(), single-instance bus, CLI parsing, DI wiring, crash handler
  ui/                    # PRESENTATION — Qt Widgets only. No document mutation, no engine calls.
    shell/               #   main window, ribbon, QAT, document tab bar, status bar
    viewer/              #   page view widget, tile presenter, selection & tool overlays
    panes/               #   thumbnails, bookmarks, comments, fields, layers, tags, signatures, search
    dialogs/             #   properties, security, preferences, batch, export…
    write/               #   the Write-mode editing surface (reflow canvas, ruler, style gallery)
  core/                  # PLATFORM- AND UI-AGNOSTIC DOMAIN. Builds and tests headless.
    doc/                 #   Document, Page, Annotation, FormField, Bookmark, Outline, Layer, Tag
    cmd/                 #   Command, CommandJournal, ActionRegistry, KeyContext, dispatch
    reflow/              #   the Write-mode document model + the fixed↔reflow conversion
    text/                #   extraction, layout analysis, selection model, search, indexing
    security/            #   permissions, encryption policy, signature build/validate LOGIC
    render/              #   tile geometry, DPI policy, render request scheduling & cache policy
    batch/               #   action sequences, runners, reports
    settings/            #   typed settings store
    backend.h            #   ↙ INTERFACES core needs from the engine (see 4.2)
  pdf/                   # PDF ENGINE — the only code that may include PDFium or write PDF bytes
    cos/                 #   first-party: lexer, object model, xref, filters, crypt, incremental writer
    render/              #   PDFium facade: open, page tree, rasterize, extract text
    ops/                 #   engine-side realization of each mutation (annots, pages, fields, redact)
  platform/              # OS ABSTRACTION
    platform.h           #   interfaces only (see 4.3)
    win/                 #   the Windows backend — the only OS-specific code
  i18n/                  # .ts / .qm string catalogs (DATA)
  res/                   # icons, cursors, stamp templates, default profiles (DATA)
  corpus/                # test PDFs incl. malformed, encrypted, signed, XFA (DATA)
  golden/                # reference page rasters for the render tests (DATA)
  tools/                 # layer-lint, i18n-lint, license-audit, golden-update, vcpkg ports
  tests/                 # unit, golden, roundtrip, fuzz targets
  cmake/                 # toolchain-mingw.cmake (the pinned GCC), warning flags, deploy helpers
  CMakeLists.txt  CMakePresets.json  vcpkg.json
```

### 4.2 The dependency direction — inward only, stated once

```
                    app/            (composition root: creates every concrete, wires nothing else)
                      │ owns & injects
        ┌─────────────┴─────────────┐
        ▼                           ▼
      ui/ ──────uses──────▶      core/ ──────uses──────▶  core/backend.h   (interfaces)
                                    │                     platform/platform.h  (interfaces)
                                    │                              ▲            ▲
                                    │                    implements│            │implements
                                    ▼                              │            │
                              (nothing outward)                  pdf/     platform/win/
```

**The rule, in full:**

- `core/` depends on **nothing** but the standard library, `QtCore` (for `QString`, containers, and
  signals — a deliberate, documented exception), a short allowlist of **portable pure-computation
  libraries** (ICU, HarfBuzz, Hunspell — headless, deterministic, no OS or UI surface; the reflow and
  text engines genuinely need them and putting shaping behind an interface would buy nothing), and
  its own two interface headers. It **never** includes `ui/`, `pdf/`, `platform/win/`, `QtWidgets`,
  `QtGui`, PDFium, or any OS header. `tools/layer-lint` enforces the allowlist, so extending it is a
  deliberate act, not a drift.
- `core/` reaches the PDF engine only through `core/backend.h`, which declares
  `IDocumentBackend`, `IPageRasterizer`, `ITextExtractor`, `ISigner`, `IOcrEngine`, `IIndexer`.
- `core/` reaches the OS only through `platform/platform.h`.
- `pdf/` implements `core/backend.h`. It is the **only** place a `FPDF_*` symbol or a PDF byte write
  may appear. It does not know `ui/` or `core/`'s concrete classes exist beyond the interface header.
- `ui/` may use `core/` and the interface headers. It may **never** include `pdf/` or
  `platform/win/`, and it may **never** mutate a document except by constructing a `Command` and
  handing it to the journal.
- `platform/win/` implements `platform/platform.h` and nothing else.
- `app/` is the **only** translation unit that names a concrete backend or a concrete platform
  implementation.
- `i18n/`, `res/`, `corpus/`, `golden/` are **data, not code**. Nothing links them; they are loaded
  at runtime or by tests.

This is machine-checked, not trusted: `tools/layer-lint` fails CI on any include that crosses a
boundary the wrong way, on any `FPDF_` outside `pdf/`, and on any filesystem write outside `pdf/`
and `core/settings/`.

### 4.3 What Qt gives us for free, versus what needs platform code

The abstraction in `platform/` is derived from the right-hand column of this table and from
[`Features.md` §10](./Features.md#10-portability-note-windows-first-portable-design) — **not** from
speculation. If Qt already does something cross-platform, it does **not** get an interface.

**Qt already provides, cross-platform — so these do NOT go behind `platform/`:**
windows, dialogs, docking, tabs, menus, and toolbars · event loop and signals · file I/O and
watching · settings files · text layout and font rendering for *UI* text · basic clipboard
(text/image) · drag-and-drop · network (for updates, timestamp servers, OCSP, LDAP over TCP) ·
XML and JSON · threading and thread pools · `QPrinter`/`QPrintDialog` for *ordinary* printing ·
image codecs for UI assets · translation loading · per-monitor DPI handling for *widgets* ·
process launching · SVG icon rendering · high-precision timers.

**Needs platform code — this list *is* the justification for `platform/`:**

| Interface | Why Qt cannot do it |
|---|---|
| `IShellIntegration` | File-type registration, context-menu verbs, thumbnail provider, preview handler, and a content-index filter are COM server registrations. |
| `IPrinterDriverService` | Installing and servicing a **virtual PDF printer** is a print-monitor/driver task, entirely outside Qt. |
| `IScanner` | Qt has no imaging-device API. |
| `ICertificateStore` / `ICryptoProvider` | Signing must use the OS store, smart cards, and HSMs through the platform's crypto provider; Qt exposes no such surface. |
| `ISecretStore` | Qt has no OS-key-protected secret storage. |
| `ISpeech` | Qt has no TTS in the modules we take. |
| `IFontResolver` | Resolving a *fallback for a missing embedded document font* requires the platform's font-matching engine; Qt's font DB serves UI text, not document glyph substitution. |
| `IAcceleratedSurface` | Presenting rendered tiles through the platform's accelerated 2D API, with a software fallback. |
| `IRichClipboard` | Vector-metafile and HTML clipboard flavours beyond `QClipboard`'s text/image. |
| `ISingleInstanceBus` | Qt's local-socket trick works, but the file-open handoff and activation-to-foreground are OS behaviours worth isolating. |
| `IOfficeBridge` | Driving the installed office suite and hosting its add-ins is COM automation. |
| `IBrowserBridge` | The browser extensions' native-messaging host is an OS registration. |
| `IRightsManagement` | Enterprise information-protection labels come from a platform SDK. |
| `IProcessSandbox` | Launching a restricted child process for protected view uses OS job/token APIs. |
| `IPowerSession` | Sleep/resume, battery-saver render throttling, and session lock notifications. |
| `IDisplayTopology` | Monitor enumeration with per-monitor DPI and topology-change notification, beyond what `QScreen` reports. |

Sixteen interfaces sounds like a lot until you notice the shape: it is **wide and shallow**. Most
are three to six methods and none of them is in the document-editing path. That is the correct
trade for this product — the OS surface is broad because the *feature set* is broad, but the
document engine stays entirely platform-free and therefore entirely unit-testable.

---

## 5. The headline cross-cutting concern, designed end-to-end: mutation-through-commands

### 5.1 The problem it solves

A PDF editor has dozens of mutation entry points — the ribbon, panes, page-view tools, form filling,
batch sequences, the command line, form JavaScript, and Write-mode typing. If each is allowed to
change the document its own way, then undo becomes partial, crash recovery becomes impossible,
incremental save cannot know what changed, a signature can be silently invalidated, and no test can
assert that an operation is reversible. Every one of those failures is architectural, not
incidental. So the mutation path is designed **now**, once.

### 5.2 The rule

> **Every change to a document is a `Command`, applied through the `CommandJournal`. There is no
> other way to change a document. No code in `ui/` or `core/` may write PDF bytes.**

### 5.3 Runtime component and API

```cpp
// core/cmd/Command.h
class Command {
public:
    virtual ~Command() = default;
    virtual QString  name()  const = 0;          // for the undo menu; goes through tr()
    virtual Scope    scope() const = 0;           // which pages/objects it touches
    virtual bool     apply (Document&, MutationSink&) = 0;   // forward
    virtual bool     revert(Document&, MutationSink&) = 0;   // exact inverse
    virtual QJsonObject serialize() const = 0;    // for the recovery journal & batch replay
    static  std::unique_ptr<Command> deserialize(const QJsonObject&);
};

// core/cmd/CommandJournal.h
class CommandJournal {
public:
    bool  run(std::unique_ptr<Command>);   // apply + push onto undo, clear redo, append to recovery
    bool  undo();
    bool  redo();
    void  beginGroup(const QString& name); // compound ops (e.g. "Insert 12 pages") undo as one
    void  endGroup();
    bool  isDirty() const;
    Revision revision() const;             // monotonic; render caches key off this
    void  markSaved(Revision);
signals:
    void  mutated(Scope);                  // renderers/panes invalidate exactly what changed
    void  dirtyChanged(bool);
};
```

`MutationSink` is the *only* channel from a command to the engine. It is an interface declared in
`core/backend.h` and implemented in `pdf/ops/` — so a command expresses **intent**
(`setPageRotation(3, 90)`), and the engine decides how that becomes objects and bytes. A command
can be unit-tested against a mock sink with no PDF at all.

### 5.4 On-disk format

Two artifacts, deliberately separate:

**The document.** Saving appends an **incremental update** to the existing file: the changed
indirect objects, a new cross-reference section, and a trailer whose `/Prev` points at the previous
one. The original bytes are never rewritten. This is what makes signature preservation possible
(§3.3 A4) and what makes a save of one annotation cost kilobytes rather than the whole file.
`Save As` performs a **full rewrite** with object renumbering and compaction, and is the only path
that discards revision history. A document opened read-only or with insufficient permissions
refuses `run()` before a command is constructed, not after.

**The recovery journal.** `%LOCALAPPDATA%\Recto\recovery\<docid>\` holds `meta.json` (source path,
size, mtime, hash, the revision last saved) and `journal.ndjson` — one serialized command per line,
appended and `fsync`ed as each command runs. On launch, any journal whose `meta.json` still matches
its source file is offered for replay; each command deserializes and re-applies in order. A journal
whose source no longer matches is offered as "recover to a new file" instead of silently replaying
onto changed content.

### 5.5 Fallback behaviour

- A command whose `apply()` returns false leaves the document untouched and is not journalled —
  partial application is a bug, and a debug build asserts on it.
- A command that cannot be serialized (`serialize()` returns an empty object) still works for
  undo/redo but marks the journal **non-recoverable** from that point, and the UI says so.
- Replay of a recovery journal that fails at command *k* stops there, keeps the first *k-1*, and
  reports which command failed rather than discarding the whole recovery.
- A malformed source PDF is repaired on open into a *reconstructed* object model; the repair report
  is attached to the `Document` and shown in the message bar, and the first save is forced to
  `Save As` so a damaged original is never appended to.
- If the recovery directory is unwritable, editing still works — the journal degrades to
  in-memory-only and the UI warns once.

### 5.6 How it stays enforced

Four independent gates, because a convention that depends on reviewer attention will not survive
a hundred stages:

1. **`tools/layer-lint`** (CI, blocking) — fails on: any `FPDF_` symbol outside `pdf/`; any
   `QFile`/`std::ofstream`/`CreateFile` write outside `pdf/` and `core/settings/`; any `#include`
   of `ui/` from `core/` or `pdf/`; any `#include` of `pdf/` or `platform/win/` from `ui/` or
   `core/`.
2. **`tools/mutation-lint`** (CI, blocking) — every `Command` subclass must be listed in
   `core/cmd/CommandRegistry.cpp`; every registered command must have a round-trip
   serialize/deserialize test and a revert test. A new command with no test fails the build.
3. **A reversibility property test** (unit) — for each registered command, over the corpus:
   `apply` then `revert` must restore a structural hash of the document exactly.
4. **A save-fidelity round-trip test** — open → run a command → incremental save → reopen →
   assert the mutation is present, the untouched objects are byte-identical, and any pre-existing
   signature still validates.

### 5.7 Build-time check

`cmake --build --target checks` runs `layer-lint`, `mutation-lint`, `i18n-lint` (D6), and
`license-audit` (§8). CI runs `checks` **before** compiling, so a layering violation fails in
seconds rather than after a full build.

---

## 6. Dependencies & build

### 6.1 Qt 6.11.1 modules

`QtCore` · `QtGui` · `QtWidgets` · `QtNetwork` (updates, timestamp/OCSP/LDAP) · `QtSvg` (icons) ·
`QtPrintSupport` · `QtConcurrent` · `QtMultimedia` (audio comments, video annotations) ·
`QtLinguistTools` (build-time only). **All nine are present in the installed prefix** — verified
2026-08-17 against `C:\Qt\6.11.1\mingw_64\lib\cmake`, so no module gap blocks any stage.

**Explicitly not taken:** `QtWebEngine` (enormous, and we render PDFs ourselves), `QtCharts` and
`QtDataVisualization` (**GPL-3.0 only under the open-source license — taking either would breach
D4**), `QtQuick`/QML (D2).

⚠️ **`QtCharts` is installed on this machine** (`qt.qt6.6111.addons.qtcharts`), sitting in the same
prefix as everything we do use. Having it on disk breaches nothing — only *linking* it would — but it
means the D4 trap is one `find_package(Qt6 COMPONENTS Charts)` away rather than an install step away.
`tools/license-audit`'s Qt-module allowlist is therefore load-bearing from Stage 0, not a formality
for later, and its fixture set must include "a build that links Qt Charts" as a case that fails.

### 6.2 Third-party libraries

| Library | License | Purpose |
|---|---|---|
| **PDFium** | BSD-3-Clause | PDF parsing, rasterization, text extraction, AcroForm rendering (D3) |
| zlib | zlib | `FlateDecode` |
| libjpeg-turbo | BSD-3 / IJG | `DCTDecode` |
| OpenJPEG | BSD-2 | `JPXDecode` |
| libtiff | libtiff (BSD-like) | TIFF import/export |
| libpng | libpng | PNG import/export |
| FreeType | FTL or GPL-2 → **take FTL** | Document glyph rasterization for embedded fonts |
| HarfBuzz | MIT | Complex-script shaping in the reflow engine |
| ICU | Unicode-DFS | Collation, boundary analysis, transliteration, encoding conversion |
| Hunspell | **LGPL-3.0** — upstream is `MPL-1.1 OR LGPL-2.1-or-later OR GPL-2.0-or-later`; **we elect the LGPL branch, taken as 3.0** (see note) | Spell checking in Write mode and in comments |
| OpenSSL 3.x | Apache-2.0 | PDF encryption primitives, PKCS#7/CMS signatures, OCSP, timestamps |
| Tesseract + Leptonica | Apache-2.0 / BSD-2 | OCR (a later stage; interface declared early) |
| Xapian | **GPL-2.0 → REJECTED** | — full-text index; use a first-party inverted index instead (see R2) |
| Catch2 v3 | BSL-1.0 | Unit tests |
| WiX Toolset v4 | MS-RL | MSI authoring |
| **libstdc++ / libgcc** (GCC runtime, shipped as DLLs) | **GPL-3.0-or-later WITH GCC-exception-3.1** | The C++ standard library and unwinder our binaries link against (D1) |
| **mingw-w64 runtime** (`libwinpthread`, CRT import libs, Win32 headers) | Permissive mix — public domain / ZPL-2.1 / MIT / BSD-2, recorded per component | Win32 API headers, import libraries, and threading runtime |

**Hunspell's licence, corrected 2026-08-18 (Stage 0, task T8).** This table previously said
"MPL-2.0", which upstream does not offer. Verified against
[`license.hunspell`](https://github.com/hunspell/hunspell) and vcpkg's port manifest, the grant is
**`MPL-1.1 OR LGPL-2.1-or-later OR GPL-2.0-or-later`**. We elect the **LGPL** branch, and because that
grant reads *"or later"* we take it specifically as **LGPL-3.0** — the identifier §8.2 already
allows, and precisely Qt's licence. One obligation model therefore covers both libraries: dynamic
linking, user-replaceable binaries, and a corresponding-source offer, all of which the release gate
already tests for Qt. MPL-1.1 would have been the lighter election (file-level copyleft, no relinking
obligation), and was declined because a second, differently-shaped obligation set costs more to
comply with than the stricter licence does. **§8.2's allowlist needs no change** — which is the point:
the gate was not satisfied by widening it.

**Every entry above is permissive except Qt, Hunspell (both LGPL-3.0), and the GCC runtime, which is
GPL-3.0 *with the Runtime Library Exception* — and that exception is the whole reason it is allowed
here.** See §8.1; this is
the one place D4's flat "no GPL in the tree" rule needs a named, reasoned carve-out rather than a
substring match. The two genuine GPL-family traps found while assembling this list — Qt
Charts/DataVisualization and Xapian — are recorded explicitly so nobody adds them later believing
they were merely overlooked.

### 6.3 Build and dependency management

- **CMake ≥ 3.25**, one `CMakeLists.txt` per layer, layers as separate static libraries
  (`recto_core`, `recto_pdf`, `recto_platform`, `recto_ui`) linked by one `recto` executable.
  Making them separate targets is what lets the linker itself catch some layering violations.
- **Toolchain, pinned (D1, A7).** `cmake/toolchain-mingw.cmake` points at
  `C:\Qt\Tools\mingw1310_64` — **GCC 13.1.0, `x86_64-w64-mingw32`, SEH, posix threads, msvcrt** — and
  asserts version, triple, and CRT at configure time. **The pin is Qt's own bundled compiler**, so
  "does our toolchain match Qt's" is true by construction rather than by discipline. A developer who
  substitutes an MSYS2 UCRT64 or newer MinGW-Builds GCC gets a configure failure, which is the whole
  point. The prefix path itself is a cache variable with that default, not a hard-coded string.
- **The Qt install is the build environment.** CMake **3.30.5** (`C:\Qt\Tools\CMake_64`) and Ninja
  **1.12.1** (`C:\Qt\Tools\Ninja`) ship with it and satisfy D7's ≥ 3.25; `windeployqt` is at
  `C:\Qt\6.11.1\mingw_64\bin`. A build machine needs Qt and vcpkg, nothing more. The bundled
  binutils cover the two toolchain-specific jobs this plan depends on: `gendef` + `dlltool` for
  PDFium's import library (A6), and `gdb` + `addr2line` for DWARF symbolization.
- **`CMakePresets.json`**: `x64-debug`, `x64-release`, `arm64-release`, plus two presets that do not
  run on Windows — `portable-asan` and `portable-fuzz` (see *Sanitizers and fuzzing* below).
- **Flag set** — the intent-by-intent translation is tabulated at the end of this section.
- **vcpkg manifest mode** (`vcpkg.json` with a pinned baseline commit) on **`x64-mingw-dynamic`**, so
  builds are reproducible. Any dependency vcpkg lacks — or lacks *on that triplet* (A5) — gets a
  vendored port in `tools/ports/`.
- **Qt is not built by vcpkg.** It comes from Qt's official Windows MinGW distribution, both because
  vcpkg's Qt-on-MinGW path is fragile and because D8 requires us to ship a Qt the user can rebuild
  and drop in — an upstream binary is the easiest such build to reproduce.
- **PDFium is consumed as a DLL through its C API** (A6). No C++ object, exception, `FILE*`, or
  heap pointer ownership crosses that boundary — a rule the `pdf/render/` facade already enforces
  for design reasons and that now also carries an ABI reason.
- **Warnings are errors** (`-Werror`) in CI for `core/` and `pdf/`; `ui/` gets a grace list that must
  shrink. Note that a GCC upgrade can introduce new warnings where MSVC would not have — the
  toolchain pin above is what keeps `-Werror` from turning into a random build break.
- **Sanitizers and fuzzing — the one real capability loss.** MinGW-w64 GCC ships **no** ASan, UBSan,
  or TSan runtime, and GCC has no libFuzzer. Confirmed on the pinned toolchain, not assumed: a
  recursive search of `C:\Qt\Tools\mingw1310_64` finds no `*asan*` or `*ubsan*` runtime at all. Coverage is preserved by moving those gates to code
  that is portable *by rule*: `core/` must already build with no display and no PDF engine, and
  `pdf/cos/` is first-party portable C++. So `portable-asan` and `portable-fuzz` build **`core/` +
  `pdf/cos/` + their unit and fuzz targets on Linux** (GCC/clang) with ASan+UBSan and libFuzzer.
  This costs a CI job and buys back both gates — and it mechanically proves the portability the
  architecture already claims. For Windows-only code (`platform/win/`, `ui/`) the substitutes are
  Application Verifier, Dr. Memory, `_GLIBCXX_ASSERTIONS`, and `-fstack-protector-strong`. An
  `x64-clang-asan` preset over LLVM-MinGW (whose ASan *does* work on Windows) is the documented
  escape hatch if a leak is ever localized to Windows-only code. **Open decision O10.**
- **Debug info is DWARF, not PDB.** Nothing in the toolchain writes a `.pdb`, so a minidump is not
  symbolized by the usual Windows tooling out of the box: release builds keep their unstripped
  binaries in a build-ID-keyed symbol archive, and `app/CrashHandler`'s reports are symbolized from
  DWARF. Budget this at Stage 1 when the crash handler is written, not at Stage 15 when the first
  field crash arrives.
- **Packaging**: `windeployqt` copies the Qt DLLs beside `recto.exe` (`--compiler-runtime` brings the
  GCC runtime DLLs with them); WiX packages that tree; a separate MSIX target wraps it for per-user
  install. The virtual printer driver and the shell extensions are separate MSI features,
  individually installable — see R8 for the parts of that surface a GCC toolchain does not reach.

**Flag translation** from the MSVC formulation this plan previously carried, so that nothing is
silently dropped in the move:

| Intent | Was (MSVC) | Now (g++) |
|---|---|---|
| Warning level | `/W4` | `-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor` |
| Standards conformance | `/permissive-` | g++'s default (conforming lookup, no MS extensions); keep `-fno-ms-extensions` explicit so nobody turns it back on |
| Language version | `/std:c++20` | `-std=c++20` — **not** `gnu++20` |
| Source / execution charset | `/utf-8` | `-finput-charset=UTF-8 -fexec-charset=UTF-8` |
| Large object files | `/bigobj` | `-Wa,-mbig-obj`, which debug builds of template-heavy TUs will need |
| Unicode entry point | `/ENTRY:wWinMainCRTStartup` | `-municode` |
| GUI subsystem, no console window | `/SUBSYSTEM:WINDOWS` | `-mwindows` |
| Dead-code stripping | `/OPT:REF` | `-ffunction-sections -fdata-sections -Wl,--gc-sections` |
| Static analysis | `/analyze` | ~~`-fanalyzer` on `core/` and `pdf/cos/`~~ **— not viable, see below** — **clang-tidy** is the route |
| Debug symbols | `/Zi` → `.pdb` | `-g` → DWARF in the binary, in **every** configuration including Release; see the symbol-archive note above |

**`-fanalyzer` measured and dropped, 2026-08-18 (Stage 0, task T2).** GCC's analyzer is built for C.
On the pinned GCC 13.1.0 it false-positives on the simplest possible C++:

```
int main() { std::string s = "x"; return s.size(); }
warning: leak of 's.…basic_string<char>::_M_dataplus.…_M_p' [-Wanalyzer-malloc-leak]
```

Since this table puts it on `core/` and `pdf/cos/` — precisely the two layers that carry `-Werror` —
enabling it would fail the build on any file that touches `std::string`. Suppressing the noise with
`-Wno-analyzer-*` would leave a flag that looks like static analysis and performs none, which is worse
than not having it. **clang-tidy, already named in the same row, is the static-analysis route**; it is
scheduled with the first real `core/` code. `cmake/RectoWarnings.cmake` records this at the point
where a reader would otherwise wonder why the flag is missing.

---

## 7. Recommended first milestone (foundation vertical slice)

Chosen for **risk retirement, not feature count**. It is a deliberately unimpressive product — you
can open a PDF, look at it, rotate one page, undo that, and save — but it drives a signal through
**every** architecturally dangerous seam at once, so that if any of the five big decisions is wrong
we learn it in the first weeks, when it is still cheap.

1. **App shell.** `main()` → composition root → main window with a ribbon, a document tab bar, and
   a status bar. One typed settings store. The action layer with `KeyContext` dispatch. String
   catalogs wired with a non-English locale actually loaded.
   → *Proves D2, D6, A3, and the composition-root discipline.*
2. **Open and render.** `pdf/render/` opens a file through the PDFium facade; `core/render/`
   computes tile geometry at the active monitor's physical DPI; a worker pool rasterizes; the viewer
   presents tiles. Scroll, zoom, and page-layout modes work; the window is dragged between two
   monitors at different scaling and re-renders correctly.
   → *Proves D3's read half, D10, A2, and the per-monitor DPI story — the single most likely place
   to discover the design is wrong.*
3. **One real mutation, reversibly.** `RotatePageCommand` runs through the journal:
   `ui/` constructs it, `core/` applies it, `pdf/ops/` realizes it, the viewer invalidates only the
   affected page, `Ctrl+Z` puts it back.
   → *Proves D5's runtime half, the `MutationSink` boundary, and that `ui/` can cause a change
   without touching the engine.*
4. **Incremental save, and get the bytes back.** Save writes an incremental update through
   `pdf/cos/`'s writer; reopening shows the rotation; a diff shows the original bytes are intact;
   a pre-signed corpus file still validates its signature after an unrelated incremental edit.
   → *Proves D3's write half — our own COS layer, the riskiest first-party code in the project —
   plus A4.*
5. **Crash and recover.** The recovery journal is written as the command runs; the process is
   killed; the next launch offers and replays the rotation.
   → *Proves §5.4's journal and §5.5's fallbacks.*
6. **One platform seam.** Double-clicking a `.pdf` in Explorer opens it in the already-running
   instance via `ISingleInstanceBus`, with `platform/win/` the only OS-aware code involved.
   → *Proves the `platform/` boundary is real and that `app/` is the only integrator.*

**What this slice deliberately does not include:** any annotation, any form, any text editing, Write
mode, security, or export. None of those carry architectural risk once 1–6 hold; all of them carry a
lot of *work*. Ordering work before risk is how architectures get discovered to be wrong in month
nine.

**Go/no-go gates on the slice.** If (2) cannot hold p95 tile latency under ~16 ms at the target DPI,
revisit D10's tiling policy before building anything on it. If (4) cannot preserve a signature,
revisit the incremental-writer design before a single annotation is written.

---

## 8. Licensing & compliance

### 8.1 Obligation → concrete action

**Qt 6.11.1 under LGPL-3.0** — the only copyleft-family license in the tree apart from the GCC
runtime's exception below, and the whole reason D8 exists:

| Obligation | Concrete action |
|---|---|
| The library must be **replaceable** by the user with a modified version. | Ship Qt as **DLLs beside `recto.exe`**, deployed by `windeployqt`. **Never** `-static`. Never sign-lock, checksum-verify, or otherwise anti-tamper the Qt DLLs. A CI check asserts no Qt symbol is statically linked into `recto.exe`. |
| Convey the LGPL-3.0 and GPL-3.0 license texts. | `res/licenses/LGPL-3.0.txt` and `GPL-3.0.txt` installed to `<installdir>\licenses\`, listed in the MSI manifest. |
| State that Qt is used and under which license. | Help → About → **Third-Party Notices**, generated at build time from `tools/license-audit`'s output — so a new dependency cannot ship without appearing there. |
| Offer the corresponding source of the Qt version we ship, including any patch we apply. | Host the exact Qt source tarball and any patch series at a stable URL; the About dialog links it; the URL and the Qt commit hash are recorded in `THIRD-PARTY.md` per release. **If we ever patch Qt, that patch must be published.** |
| Do not use Qt modules whose only open-source license is GPL-3.0. | `tools/license-audit` fails the build on any link against Qt Charts, Qt Data Visualization, Qt Virtual Keyboard, or any module its allowlist does not name. |
| Allow reverse-engineering for the purpose of debugging modifications to the library. | Do not add an EULA clause forbidding it. **Legal review owns this clause.** |

**PDFium, zlib, libjpeg-turbo, OpenJPEG, libtiff, libpng, HarfBuzz, ICU, OpenSSL, Tesseract,
Leptonica, Catch2 (BSD/MIT/Apache-2.0/zlib/BSL family)** — retain each copyright notice and license
text in `res/licenses/`, reproduce them in the Third-Party Notices dialog and in the installed
`licenses\` folder. Apache-2.0 additionally requires shipping any `NOTICE` file verbatim — the audit
tool checks for one per dependency.

**FreeType (dual FTL / GPL-2.0)** — record the FTL election explicitly in `THIRD-PARTY.md`; FTL
requires a credit in the documentation, which the Notices dialog provides.

**The GCC runtime (libstdc++, libgcc) — GPL-3.0 *with* the Runtime Library Exception.** D1 puts a
GPL-3.0-licensed library into every binary we ship, so this needs to be stated precisely rather than
discovered by an auditor:

| Obligation / condition | Concrete action |
|---|---|
| The Runtime Library Exception permits distributing a program under **any** license, including proprietary, provided the program was produced by an **Eligible Compilation Process**. | Compile only with stock GCC. **Never add a non-free GCC plugin to the build** — a proprietary plugin voids the exception and would retroactively make our own source GPL-3.0. This is a one-line rule with an unbounded downside, so `tools/license-audit` also asserts the compiler's plugin list is empty. |
| Record which runtime version we ship and under which terms. | The GCC runtime DLLs are listed in `THIRD-PARTY.md` with their exact version, the `GPL-3.0-or-later WITH GCC-exception-3.1` identifier, and the exception text in `res/licenses/GCC-exception-3.1.txt`. |
| Do not let the audit tool's no-GPL rule reject it — or, worse, let someone "fix" the rejection by dropping the entry. | The allowlist in §8.2 names the identifier **with its exception** explicitly. `tools/license-audit` must match whole SPDX expressions, **never a `GPL` substring**, or it will both false-positive here and false-negative on `LGPL-3.0`. |
| Keep the runtime replaceable, as for Qt. | Ship `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, and `libwinpthread-1.dll` beside the executable. **Never `-static-libstdc++` or `-static-libgcc`**: besides the replaceability argument, a statically linked libstdc++ alongside a dynamically linked Qt built against the shared one is an ABI hazard in its own right. |

**Recto's own source** — proprietary. No file in `core/`, `pdf/`, `ui/`, `app/`, or `platform/` may
be copied from a copyleft project. Any code arriving from outside the team needs a recorded
provenance line in `THIRD-PARTY.md` before it merges.

### 8.2 Third-party license register

`THIRD-PARTY.md` is generated, not hand-written: `tools/license-audit` walks the resolved vcpkg
manifest plus the Qt module list and emits, per dependency, its **name, exact version, license
SPDX identifier, upstream URL, source-offer URL where required, and whether a NOTICE file exists**.

**When the source-offer URL is expected to exist.** Not before there is a release to host a tarball
against. Until then each LGPL-3.0 component — currently **Qt** and **Hunspell**, the latter by our
own election (§8.1) — carries its obligation in `spdx-map.json` as a `TODO-BEFORE-RELEASE` string
naming what must be hosted and where it must be linked from, and `THIRD-PARTY.md` reproduces it
verbatim under *Source offers*. That is the required state for every stage before 15: the obligation
**stated, attached to its component, and visible in the generated file**. An obligation that has
silently disappeared from the register is a defect at any stage; one that is written down and
unfinished is not. §8.3 is where it stops being acceptable — the release gate below turns each entry
into a URL that must resolve and serve the exact shipped version.
The build fails if a dependency's SPDX identifier is not on the allowlist
(`BSD-2-Clause, BSD-3-Clause, MIT, Apache-2.0, Zlib, BSL-1.0, Unicode-DFS-2016, FTL, LGPL-3.0,
libpng-2.0, libtiff, IJG, MS-RL, ZPL-2.1, GPL-3.0-or-later WITH GCC-exception-3.1`) — which is what
mechanically enforces D4. The last entry is the GCC runtime and is allowed **only** in that exact
expression: bare `GPL-3.0-or-later` is a build failure, so dropping the exception (by adding a
non-free compiler plugin, or by pulling a differently-licensed runtime) fails the audit rather than
passing silently. Matching is on the whole expression — see §8.1's note on substrings.

### 8.3 Release gate (must all be ticked)

- [ ] `tools/license-audit` passes; no SPDX identifier outside the allowlist appears anywhere.
- [ ] No GPL or AGPL dependency is present, transitively included, or dynamically loaded — the GCC
      runtime under its Runtime Library Exception (§8.1) being the one named, reasoned exception.
- [ ] The build used stock GCC with **no non-free compiler plugin**, so the Runtime Library
      Exception holds. Recorded per release with the compiler version.
- [ ] Qt is linked **dynamically**; `objdump -p recto.exe | grep "DLL Name"` shows `Qt6Core.dll` etc.
      as imports.
- [ ] The GCC runtime DLLs are shipped beside the executable, not statically linked
      (`objdump -p` lists `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`).
- [ ] **No `Qt6Charts.dll` / `Qt6DataVisualization.dll` among the imports or in the shipped tree.**
      Both are GPL-3.0-only and Charts *is installed in our Qt prefix*, so this is a real hazard
      rather than a theoretical one.
- [ ] Qt DLLs ship beside the executable, unsigned-by-us and replaceable; swapping in a
      user-rebuilt `Qt6Widgets.dll` of the same version still launches the app. **This is tested,
      not assumed.**
- [ ] LGPL-3.0 + GPL-3.0 texts and every third-party license and NOTICE file are installed.
- [ ] The About → Third-Party Notices dialog matches the generated `THIRD-PARTY.md` exactly.
- [ ] **Every LGPL-3.0 component's** source-offer URL resolves and serves the exact shipped version,
      and any patch we carry is published there. That is **Qt and Hunspell** as of Stage 0 — Hunspell
      because §8.1 elects its LGPL branch — and any component a later stage adds under that licence.
      Until this gate, each is carried as a `TODO-BEFORE-RELEASE` entry in `spdx-map.json` (§8.2).
- [ ] `THIRD-PARTY.md` for this release is committed and tagged.
- [ ] The EULA has no clause forbidding reverse-engineering for LGPL debugging purposes.
- [ ] No bundled font, icon, stamp template, or corpus file lacks a recorded license.
- [ ] The corpus and golden directories contain no customer or third-party confidential document.

---

## 9. Verification

**Build.**
```
cmake --preset x64-release
cmake --build --preset x64-release --target checks      # layer-lint, mutation-lint, i18n-lint, license-audit
cmake --build --preset x64-release
ctest --preset x64-release --output-on-failure
```
`checks` must pass before compilation; a layering violation fails in seconds.

**Toolchain (D1, A7).** `cmake --preset x64-release` fails at configure time on anything but
GCC 13.1.0 from `C:\Qt\Tools\mingw1310_64`. `g++ -v` output is recorded in the build log per release.

**Stack (D2, D8).** `objdump -p build/recto.exe | grep "DLL Name"` lists `Qt6*.dll`,
`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`, and **`msvcrt.dll` — never
`ucrtbase.dll`**, which is the cheapest possible check that the CRT still matches Qt's. No
unexpected DLL, and no `Qt6Charts.dll` (D4). Replace `Qt6Widgets.dll` with an independently built
one of the same version → the app still launches.

**Read path (D3, D10, A2).** Open each file in `corpus/` → every page renders with no crash, and the
`portable-asan` job reports no leak over the same corpus in `core/` and `pdf/cos/`.
`tests/bench_render` reports p50/p95 per-tile milliseconds at 96/144/192 DPI;
p95 < 16 ms on the reference machine is the gate. Drag the window between a 100 %- and a
150 %-scaled monitor → text stays crisp, layout does not jump.

**Mutation and undo (D5).** `ctest -R reversibility` — every registered command, over the corpus,
`apply`+`revert` restores the structural hash. `ctest -R roundtrip` — open → command →
incremental save → reopen: the mutation is present, untouched objects are byte-identical, and
`corpus/signed-*.pdf` still validates.

**Recovery (§5.4, §5.5).** Run a command, kill the process with the debugger, relaunch → the
recovery prompt appears and replaying reproduces the change. Corrupt `journal.ndjson` mid-file →
replay stops cleanly at the last good command and reports it. Make the recovery directory read-only
→ editing still works and warns exactly once.

**Damaged input (§5.5).** Every file in `corpus/malformed/` opens with a repair report, never a
crash, and its first save is forced to `Save As`. `tests/fuzz_cos` — built by the `portable-fuzz`
preset, since GCC cannot produce it — runs 10 minutes with no new crash before each release. A crash
it finds is reproduced on Windows through the ordinary `x64-debug` build before it is called fixed.

**Layering (§4.2).** `tools/layer-lint --explain` prints the include graph; assert no edge points
outward. Deliberately add `#include "pdf/cos/CosDict.h"` to a `core/` file → CI fails.

**i18n (D6).** Launch with `--lang zh-TW` → the UI is translated; `tools/i18n-lint` reports zero
untranslated user-facing literals and zero missing catalog keys.

**Platform boundary.** Build `recto_core` and `recto_pdf` alone, link the test binary without
`recto_platform` and without `recto_ui`, and run the full headless suite → it must pass. If it
cannot, the boundary has already leaked.

---

## 10. Open risks carried forward

| # | Risk | Carried to |
|---|---|---|
| R1 | Write mode's reflow engine (`core/reflow/`) is the largest single unknown in the product: reconstructing a semantic paragraph/table model from fixed positioned glyphs is genuinely hard, and A1 does not tell us how the reference product does it. | A dedicated late stage, with a spike before it is committed to. |
| R2 | Full-text catalog indexing needs an inverted index, and the obvious library (Xapian) is GPL. A first-party index is more work than expected. | Its own stage; interface `IIndexer` declared early so the choice stays open. |
| R3 | Export fidelity to Office formats is a product-defining quality bar and we have no engine for it. Options: first-party writers, `IOfficeBridge` round-tripping through the installed suite, or a commercial component for this subsystem only. | Decided at the export stage, on measured fidelity — not now. |
| R4 | Dynamic XFA is effectively a second document format. `Features.md` promises normalization of *static* XFA and honest reporting for dynamic; whether we ever do more is undecided. | Deferred; the honest-reporting path is the committed behaviour. |
| R5 | A2 (render throughput) is unmeasured and gates D10. | Stage 2's go/no-go. |
| R6 | Qt Widgets' ability to host a real ribbon (A3) is unproven. | Stage 1's go/no-go; fallback is a first-party ribbon widget, not a toolkit change. |
| R7 | **The GCC toolchain (D1) has no ASan/UBSan on Windows and no libFuzzer at all**, and it emits DWARF rather than PDB. Two documented quality gates and the crash-symbolization story all depend on tooling MSVC gave for free. | Mitigated, not eliminated: §6.3's `portable-asan`/`portable-fuzz` jobs recover both gates for `core/` and `pdf/cos/` — which is most of the memory-unsafe surface — leaving `platform/win/` and `ui/` covered only by Application Verifier and Dr. Memory. DWARF symbolization is Stage 1 work on `app/CrashHandler`. **O10** records which sanitizer route we settle on. |
| R8 | **Parts of the Windows integration surface are MSVC-shaped.** The virtual printer driver (WDK), Office add-ins, and the shell extensions lean on ATL/WRL, `#import` type libraries, and WDK build rules that MinGW does not provide; arm64 support in MinGW-w64 GCC is immature where LLVM-MinGW is not. | None of it is in the editing path, and `platform/platform.h` already isolates all of it behind 16 interfaces. If a component genuinely cannot be built with GCC, it becomes a **separately built satellite DLL or EXE** talking to us across the C/COM boundary the platform layer already imposes — the architecture pays for that isolation precisely here. arm64 (O6) most likely means LLVM-MinGW rather than GCC. Settle each at its own stage (15, 16), not now. |
