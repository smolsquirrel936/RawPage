# `layer-lint/` fixtures — what is covered, and what is not

Each subdirectory is a miniature repository that `layer-lint` is pointed at with `--root`.
Stub headers exist only so an include resolves inside the fixture tree.

**This page exists because of I-15.** Every fixture used one spelling of the violation, so
that was the only spelling ever tested — and three others compiled and passed clean for the
life of the gate. A list of what is *not* covered is the thing that turns the next gap into a
known one instead of a surprise. `../wrong-compiler/README.md` does the same for the
toolchain pin.

## Covered

### The outward-include rule, one tree per spelling

| Tree | Spelling |
|---|---|
| `outward-include/` | `#include "pdf/cos/CosDict.h"` — canonical, repo-relative |
| `outward-include-relative/` | `#include "../../pdf/cos/CosDict.h"` — what an IDE inserts |
| `outward-include-angled/` | `#include <pdf/cos/CosDict.h>` — compiles, since every layer target puts the source root on its include path |
| `outward-include-casemix/` | `#include "Pdf/Cos/CosDict.h"` — resolves on a case-insensitive filesystem. **Its case is Windows-only:** on Linux the include does not resolve at all, so it is a compile error rather than a layering violation |
| `ui-includes-pdf/` | the same rule in the other direction: `ui/` reaching the engine |

A backslash spelling (`#include "pdf\cos\CosDict.h"`) is handled by `std::filesystem`'s own
normalisation and needs no fixture — verified by hand, not by a case.

### The other rules

| Tree | Rule |
|---|---|
| `fpdf-outside-pdf/` | `FPDF_*` named outside `pdf/` |
| `qtwidgets-in-core/` | a QtWidgets include in `core/`, which must build with no display |
| `write-outside-allowed/` | `std::ofstream` in `ui/` |
| `qt-write/` | `QFile` + `QIODevice::WriteOnly` — matched on the **open mode**, because `QFile` alone is equally the idiomatic way to read |
| `qaction-new/` | `new QAction(parent)` — heap construction, the spelling everyone thinks of |
| `qaction-stack/` | `QAction placeholder(parent);` — the same mistake with a different lifetime, and invisible to a pattern that only looks for `new` |
| `qaction-menu-literal/` | `menu->addAction("Rotate Clockwise")` — QMenu builds the action for you, so the construction does not appear in the source at all |
| `qaction-toolbar-literal/` | `bar->addAction(icon, "Delete Page")` — the same hidden construction through QToolBar. Catching one convenience and not the other is a rule that half works |
| `backend-header-includes-platform/` | **(S2 · T1)** `core/backend.h` reaching for `platform/platform.h`. It is the one thing the `core-backend` entry narrows that the `core` entry did not already deny — `core/` at large may include the OS boundary, and the engine contract may not — and the tree's own `core/settings/Store.cpp` includes the same header legitimately, so the failure is attributable to which file reached for it. **The first version of this fixture used `fpdfview.h` and was worthless**: `core` has denied that since Stage 0, so it passed with the new entry deleted. Convention 7, and I-15's shape |

### Negative controls — without these, a gate that rejected everything would pass

| Tree | Must be **clean** |
|---|---|
| `write-escaped/` | the same write, waived inline with `allow-filesystem-write(reason)` |
| `mention-in-comment/` | prose naming an outward include, `FPDF_InitLibrary()` and `QIODevice::WriteOnly` |
| `qaction-factory-allowed/` | the legitimate construction in `ui/shell/ActionFactory.cpp`, both spellings — without this, "fires on every spelling" is equally satisfied by a rule that makes D1-7's mechanism impossible to write |
| `qaction-mention-in-comment/` | a file that discusses `QAction`, `new QAction(this)` and `menu->addAction(...)` at length in prose and constructs none |
| `pdf-impl-may-include-pdfium/` | **(S2 · T1)** `pdf/render/` including `fpdfview.h`, beside a `core/backend.h` holding buffers and interfaces. `fpdf-outside-pdf/` has proved since Stage 0 that naming PDFium outside `pdf/` fails and nothing had ever proved that naming it **inside** `pdf/` succeeds — so that case was equally satisfied by a gate rejecting every mention of PDFium in the tree, which would leave the facade unable to do its only job |

## Known **not** covered

Recorded deliberately. Each is a decision, not an oversight.

| Gap | Direction | Why it stands |
|---|---|---|
| `/* */` block comments are not tracked | **false positive** — a commented-out include inside one is still reported | Tracking them correctly means tracking string literals and raw strings too, which is a tokenizer, and a tokenizer is the compiler's job. `IncludeGraph.cpp` says so at the `splitLine` definition |
| A string literal containing `FPDF_` | **false positive** | Token matching runs over the code half of the line, string literals included. Visible and fixable when it happens |
| `QAction *ptr` written with the star against the name | **false positive** on the `bare-qaction` rule | The pattern `QAction ` catches the stack declaration, and the project writes `QAction* ptr`. A pointer declared the other way reads as a construction. Noise, and noise is the direction this rule is allowed to fail in |
| `addAction(someVariable)` used to build a *command* | **miss** on the lint | The pattern is `addAction(` and it fires — but only outside `ui/shell/ActionFactory.cpp`, which is where the one legitimate non-command use lives. A command-shaped action smuggled through that file is caught instead by `ActionFactory::wiringComplaints()` at show time, which is why D1-7 has two mechanisms rather than one |
| Computed includes — `#include SOME_MACRO` | **miss** | Unresolvable without a preprocessor. `parseInclude` returns false and says so |
| A write reached through a helper — `writeFile(path, bytes)` | **miss** | The filesystem-write rule is a substring heuristic, which is exactly why it is the only escapable one |
| A generated file that is never scanned | **miss** | `build/`, `vcpkg_installed/`, `third_party/`, `claude-plan/` and this fixture tree are excluded by path in `rules.json` |

The first two fail toward noise, which someone notices. The last three fail toward silence —
so if one of them ever matters, it needs a fixture here before it needs a fix in the tool.
