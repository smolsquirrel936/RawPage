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
| `outward-include-casemix/` | `#include "Pdf/Cos/CosDict.h"` — resolves on a case-insensitive filesystem |
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

### Negative controls — without these, a gate that rejected everything would pass

| Tree | Must be **clean** |
|---|---|
| `write-escaped/` | the same write, waived inline with `allow-filesystem-write(reason)` |
| `mention-in-comment/` | prose naming an outward include, `FPDF_InitLibrary()` and `QIODevice::WriteOnly` |

## Known **not** covered

Recorded deliberately. Each is a decision, not an oversight.

| Gap | Direction | Why it stands |
|---|---|---|
| `/* */` block comments are not tracked | **false positive** — a commented-out include inside one is still reported | Tracking them correctly means tracking string literals and raw strings too, which is a tokenizer, and a tokenizer is the compiler's job. `IncludeGraph.cpp` says so at the `splitLine` definition |
| A string literal containing `FPDF_` | **false positive** | Token matching runs over the code half of the line, string literals included. Visible and fixable when it happens |
| Computed includes — `#include SOME_MACRO` | **miss** | Unresolvable without a preprocessor. `parseInclude` returns false and says so |
| A write reached through a helper — `writeFile(path, bytes)` | **miss** | The filesystem-write rule is a substring heuristic, which is exactly why it is the only escapable one |
| A generated file that is never scanned | **miss** | `build/`, `vcpkg_installed/`, `third_party/`, `claude-plan/` and this fixture tree are excluded by path in `rules.json` |

The first two fail toward noise, which someone notices. The last three fail toward silence —
so if one of them ever matters, it needs a fixture here before it needs a fix in the tool.
