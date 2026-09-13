# Stage 2 — PDF Engine Boundary and Tiled Rendering · Execution Plan

> **What this document owns.** The **execution order** of Stage 2 and the **Stage-2-local design
> choices** that no other document decides. It owns no product facts. Every stack, layering,
> key-binding, and verification fact is cross-referenced to its owner and never restated as a new
> decision:
>
> | For… | Read |
> |---|---|
> | The Stage 2 objectives, components, key bindings handled, settings keys, and exit criteria | [`Implementation-Plan.md` § Stage 2](./Implementation-Plan.md#stage-2--pdf-engine-boundary-and-tiled-rendering) |
> | The authoritative key-binding tables (the **Viewer** context) and the page-display feature list | [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference), [§1.2](./Features.md#12-page-display) |
> | What each file is for, and flows A, B, C | [`Architecture-FileMap.md` §3.2, §3.4, §3.9, §3.10, §3.12, §3.14, §5](./Architecture-FileMap.md#34-corerender--rendering-policy-no-rasterization-here) |
> | The locked stack, the PDFium boundary rule, risk **A2**, decision **D10** | [`Tech-Stack-Plan.md` §2, §3.3, §4.3, §6](./Tech-Stack-Plan.md#2-decisions-confirmed) |
> | How Stage 2 is verified | [`Stage2-Rendering-Testing-Guide.md`](./Stage2-Rendering-Testing-Guide.md) — written from this plan, **before** the code |
>
> **Status:** ✅ **ALL EIGHTEEN TASKS COMPLETE** — T1–T3 on 2026-09-01, T4 and T5 on 2026-09-02,
> T6 on 2026-09-03, T7…T15 on 2026-09-04, T16 on 2026-09-05, T17 and T18 on 2026-09-06. The
> stage's own **sign-off** is not this document's to give: it needs the manual walk, which is the
> user's (`Stage2-Rendering-Testing-Guide.md` §9).
>
> **T18 wrote two files after all**, against its own prediction that it would write none:
> `tests/bench/bench_pipeline.cpp` and `tests/unit/tst_actionalias.cpp`, plus a `.def` fixture.
> The prediction was made when the task was believed to be measurement and paperwork; A2's product
> figure needs a program to produce it, and D2-10's two carry-overs need code. Recorded rather than
> smoothed over: **a forward-looking sentence about a task nobody has started is a guess**, which is
> this stage's most-repeated finding (I-10, and §6 of the guide, and this file's own debt list).
> Written 2026-08-31, immediately after Stage 1 signed off and before any Stage 2 code existed.
>
> **This file is a plan, not a progress log.** Stage 2's live status — task by task, deviation by
> deviation, issue by issue — is the **Stage 2 status block in
> [`Implementation-Plan.md`](./Implementation-Plan.md#stage-2--pdf-engine-boundary-and-tiled-rendering)**,
> and that block is the authority. A per-task narrative lived here until Stage 2 T16 and was removed
> then, having done exactly what a second copy does: T16 updated the task table in §6 and the manifest
> in §4 and left the header above it reading *"T1–T15 complete"*, so one file disagreed with itself in
> three places. Nothing was lost in the removal — 97 of its 98 distinctive facts were already in the
> authority, and the one that was not (T16's negative controls) was moved there first.
>
> **Label scoping, because two namespaces collide across stages.** `T1`…`T18` here are **Stage 2**
> tasks and are unrelated to Stage 0's or Stage 1's `T1`…`T15`; this document always writes
> "Stage 2 T3" when the stage is not already obvious from context. Likewise `D2-n` are this stage's
> decisions, distinct from `Tech-Stack-Plan.md`'s `D1`…`D10` and from `Stage1-AppShell-Plan.md`'s
> `D1-n`. `A2` is a **risk** (`Tech-Stack-Plan.md` §3.3); `A-nn` are **test cases**
> (`Stage2-Rendering-Testing-Guide.md` §5).

---

## 1. What Stage 2 is actually for

Stage 1 shipped the shape. Stage 2 puts something in it: a document that opens, parses, and is drawn
on screen at the right number of pixels. There are exactly **three** things that get permanently
harder if this stage gets them wrong.

1. **The A2 verdict.** Can PDFium rasterize a tile fast enough that scrolling is smooth —
   p95 under ~16 ms at the target DPI? This gates
   [`Tech-Stack-Plan.md` D10](./Tech-Stack-Plan.md#2-decisions-confirmed), the whole
   tile-on-a-worker-pool model. A no-go is a *Stage 2* cost if it is learned in the first week and a
   *Stage 6* catastrophe if it is learned when annotations are already drawing on top of tiles. →
   the two-part measurement in **D2-1**.
2. **The engine boundary.** [`Tech-Stack-Plan.md`](./Tech-Stack-Plan.md#2-decisions-confirmed) states
   the rule in one line — *we read through PDFium and write through our own code* — and it holds only
   while no PDFium type, exception, or heap ownership crosses out of `pdf/render/`. The moment an
   `FPDF_DOCUMENT` reaches `core/`, the first-party COS layer becomes optional, then decorative, then
   absent, and the project has silently become a PDFium wrapper that cannot write. `layer-lint`
   already denies `FPDF_` outside `pdf/`; **D2-11** narrows it to `pdf/render/` and adds the handle rule.
3. **Cancellation on scroll.** A tile that has left the viewport must be *dropped from the queue*,
   not rasterized and discarded.
   [`Implementation-Plan.md` § Stage 2](./Implementation-Plan.md#stage-2--pdf-engine-boundary-and-tiled-rendering)
   says the perceived-smoothness win is larger than any per-tile optimization, and that makes it
   architecture rather than tuning: a scheduler without a cancellation path is not a scheduler you
   optimize later, it is one you replace.

**What Stage 1 should change about how this stage is built.** Stage 1 produced 55 issues, and its
whole second half was one lesson repeated: **six defects — five of them S2 — were invisible to a green
suite because every case reached the behaviour through a back door** (I-34, I-42, I-43 (S3), I-47,
I-49, I-54 —
`Stage1-AppShell-Testing-Guide.md` §8). Each asked a *list* that stayed correct for a surface nobody
could see, or called a method the gesture could not reach. Convention 19 in
[`CLAUDE.md`](../CLAUDE.md) came out of it. Applied here, concretely:

- A case that asserts a tile was **requested** has not asserted it was **painted**. The offscreen
  platform can read a widget's rendered pixels back — `QWidget::grab()` — and where a case can do
  that instead of inspecting the scheduler's queue, it must.
- A case that reaches a viewer behaviour by calling `ViewportController` directly says so at the
  assertion, and names the gesture it therefore does not cover.
- I-36 is the shape to fear most: eleven green ribbon cases asked the tab bar what it *contained*
  while the layout was giving it **zero pixels**. A page view has exactly the same failure mode.
  **Assert the geometry the layout gave the widget, then assert the consequence.**

---

## 2. Inherited ledger — what Stage 1 hands over

Recorded so the tasks below are concrete. None of these is re-decided here.

| Fact | State at the end of Stage 1 | Consequence for Stage 2 |
|---|---|---|
| `rawpage.exe` | Opens a window: ribbon, QAT, tool finder, document tabs, keyboard, Preferences, log pane. **354/354** on both Windows presets, `gates` 66/66 | Stage 2 adds to a suite that is green; a red case is this stage's |
| `DocumentWindow` | A **stub** by decision D1-2 (`Stage1-AppShell-Plan.md` §3) — holds a path and a placeholder canvas, no document, no journal, no zoom, no member Stage 2 must *remove* | T13 replaces the placeholder canvas with `PageView`. The emptiness was the feature; do not preserve it |
| `MainWindow::openFiles()` | Checks that a path exists and ends in `.pdf`, **reads no bytes** (case A-58) | T9 replaces the suffix check with `IDocumentBackend::open()`. Deciding whether bytes are a PDF is the backend's job — that was the reason for the deferral, not squeamishness |
| `ShortcutBinder::activeContext()` | Ships and answers **`Global` throughout**, because *nothing in the Stage 1 shell declares a context* | **Stage 2 is the first caller of `declareContext()`** (D2-8). Until T13 the §8 Viewer table is unreachable by construction, not by defect |
| `ActionRegistry` | **361 declared ids, 23 registered.** Registration is once-per-registry and no handler captures its window (convention 14) | The Viewer commands register the same way. A handler that captures a `PageView` is convention 14's bug wearing a new hat — a second window is one tab tear-off away |
| `StatusBar` | Built; the page box, page total and zoom label come alive with a tab but the total reads an em dash, because a stub has no pages | T14 gives it real numbers. "1 of 1 at 100 %" with nothing rendered is the lie T9 refused to tell |
| `ui/shell/MessageBar` | **Does not exist.** Named in `Implementation-Plan.md` Stage 2's components | T14. It is where `RepairReport` and a password prompt surface |
| `pdf/PdfModule.cpp` | Stage 0's placeholder TU; `Stage1-AppShell-Plan.md` §10 records **"Stage 2 deletes it"** | T1 deletes it — `pdf/` gets real translation units there |
| `tools/spike-pdfium/` | ✅ Stage 0 T12, marked ⤵ throwaway: *"deleted when `pdf/render/PdfiumInit` replaces it at Stage 2"* | T2 deletes it, exactly as `tools/spike-ribbon/` was deleted at Stage 1 T10 |
| `rawpage_pdf` | One static library, one placeholder TU, linked by `rawpage_tests` under `rawpage_assert_headless()` | **It must split — see D2-2.** This is the single most likely way Stage 2 breaks a Stage 0 guarantee without anyone editing the file that states it |
| `portable-fuzz` | Wired since Stage 0; its preset description says **"NO TARGETS EXIST until Stage 2"** | ✅ **T5 gave it `fuzz_cos_lexer` and T6 `fuzz_cos_parser`, the primary one**, and `tests/fuzz/CMakeLists.txt` fails configure on the wrong target count — 1 at T5, **2 from T6**. Neither has ever been RUN — no clang on the dev box — so "proven to build one" is owed by the Linux job, not claimed here |
| `corpus/` | `make-minimal.py` + one generated `minimal.pdf`. Stage 0 §7.3 recorded **"Stage 2 owns the real corpus"** | ✅ T4 — 60 files (**61 from T16**), a `PROVENANCE.md` row each, byte-identical regeneration gated |
| `ui/` warning grace list | `RAWPAGE_UI_WARNING_GRACE` still **empty** (D1-12) | It may only shrink. `ui/viewer/` is the largest block of new `ui/` code since T10 and is where the pressure to add an entry will come from |
| Carried cases | **A-25** (`--list-actions`, a switch that never existed) and **A-26** (an alias fixture for `RAWPAGE_ACTION_ALIAS`, real code with zero rows) — carried to Stage 2 rather than closed | T18 (D2-10) |
| Open issues | **None.** I-01 … I-55, all closed | Stage 2 starts from a clean log |

---

## 3. Stage-2-local decisions

Choices this plan makes because nothing upstream decides them. Each is scoped to Stage 2 and listed
here so a later stage can find and revisit it rather than rediscovering it.

| # | Decision | Why | Revisit |
|---|---|---|---|
| **D2-1** | **A2 is measured TWICE: an early throwaway probe at T3, and a final measurement through the shipped pipeline at T18. The T18 figure is the one that is a claim about the product.** The probe is a ~150-line harness over raw `FPDF_RenderPageBitmap` at the target device resolution; the final run is `tests/bench/bench_render` driving the real `RenderScheduler`, `RenderCache` and `PdfiumRasterizer` over the whole corpus. Both figures stand in `Tech-Stack-Plan.md` §3.3, the second labelled as the product's. | This is Stage 1's own lesson, written down in advance instead of learned twice. `Stage1-AppShell-Plan.md` D1-1 answered A3 with a timeboxed spike, which was right; T15 then discovered that the spike's R-9 figure (0.28 ms) said nothing about the shipped ribbon, because *a spike's ribbon is as small as its author made it*, and re-measured against 192 real controls (0.471 ms). A probe answers **"is this physically possible"** early enough to change the architecture. Only the shipped pipeline answers **"is the product fast"**, because the scheduler, the cache and the tile geometry are between PDFium and the screen. One figure without the other is either a late verdict or a flattering one. | Never — the two-part *method* is the point. The verdict may be revisited if PDFium is upgraded (D2-3) |
| **D2-2** | **`rawpage_pdf` splits into `rawpage_cos` (first-party, portable, no PDFium) and `rawpage_pdfium` (Windows, links the DLL). `rawpage_tests` links `rawpage_cos` only, and `rawpage_assert_headless()` gains `rawpage_pdfium` in its forbidden list.** | `tests/unit/CMakeLists.txt` links `rawpage_pdf` today and asserts a closure whose stated meaning is *"must build and pass with NO DISPLAY and NO PDF ENGINE"* — but the forbidden list names only `rawpage_ui` and `rawpage_platform`. That is harmless while `rawpage_pdf` compiles one placeholder TU, and becomes false the instant `PdfiumInit.cpp` joins it: the unit suite would link PDFium while the assertion that exists to prevent exactly that kept passing. Worse, `CMakeLists.txt` adds `pdf/` on the **portable** presets too, where no Linux PDFium exists — so the `portable-asan` job would stop *configuring*, and `portable-fuzz` could never build the COS targets it was wired for. The split is not tidiness; it is what makes the sentence in that comment true, and what keeps `pdf/cos/` genuinely portable rather than portable-by-assertion. **The forbidden-list entry is the load-bearing half** — a split without it just moves the leak one edge further away. | Never. Stage 3 adds the writer to `rawpage_cos`, which changes nothing here |
| **D2-3** | **The PDFium version is pinned and the pin is asserted at configure time**, in `cmake/` beside `ToolchainAssert.cmake`'s TA-1…TA-9, reading `third_party/pdfium/VERSION` (today **153.0.8009.0**, `pdf_enable_v8=false`, `pdf_enable_xfa=false`). `tools/golden-update` refuses to regenerate a baseline without a `--reason` string, which lands in the commit message. | A golden-image baseline is a claim about **one rasterizer**. Bump PDFium without asserting the pin and every rendering regression is silently reclassified as a baseline update — the exact laundering [`Implementation-Plan.md` §2](./Implementation-Plan.md#2-cross-cutting-testing-strategy) forbids when it says baselines are regenerated *"only through `tools/golden-update` with a stated reason"*. The mechanism it names has no teeth unless the tool enforces the reason and something notices the engine moved. This is TA-1's argument applied to the second pinned dependency: Stage 0 already decided that a version this project's correctness rests on is asserted rather than assumed. | Stage 15, when a PDFium upgrade becomes a release decision with its own baseline review |
| **D2-4** | **`pdf/cos/` this stage is a READER, and it is NINE filters, not ten.** Lexer, Parser, `CosObject`, `XrefTable`, `XrefRepair`, and `Flate`/`LZW`/`Ascii85`/`AsciiHex`/`RunLength`/`Dct`/`Jpx`/`Ccitt`/`Jbig2`. **No `IncrementalWriter`, no `ObjectAllocator`** — Stage 3's own component list claims both. **No `CryptFilter`** — it is a security-handler concern and goes to Stage 10 with them. | `Architecture-FileMap.md` §3.9's filter row lists ten by naming `CryptFilter` alongside the nine; `Implementation-Plan.md` Stage 2 lists nine. The nine are right for this stage: a crypt filter without `StandardSecurityHandler` (○ S10) has no key to decrypt with, so building it here produces a file that cannot be tested and will be rewritten when the handler arrives. Reading an encrypted document at Stage 2 goes through PDFium instead — D2-6. | Stage 10 adds `CryptFilter` with the handlers; Stage 3 adds the writer |
| **D2-5** | **Four `core/doc/` types come forward from Stage 3 into Stage 2: `Revision.h`, `Scope`, `Page`, and a READ-ONLY `PageTree`.** `PageTree`'s FileMap row becomes **○ (S2 read, S3 edit)** — construct, index, count, page size, rotation and label lookup now; insert/move/remove/duplicate at Stage 3. | Not a scope grab — a dependency the plan already has and files under the wrong stage. `RenderCache::invalidate(Scope, Revision)` and `RenderRequest`-carries-a-`Revision` are **Stage 2 signatures in `Implementation-Plan.md`'s own component list**, and its *Algorithms & notes* insists tile invalidation is `Revision`-and-`Scope`-keyed *"so a stale tile can never be shown after a mutation lands in Stage 3"*. `PagePresentation::layout(const PageTree&, …)` names `PageTree` outright, and `TileGrid::tilesFor(const Page&, …)` names `Page`. `Architecture-FileMap.md` §3.2 agrees from the other side — its *Interacts with* column already says `Revision` is read by `RenderCache` and `Page` by `TileGrid`. All four are **vocabulary** rather than model: small, pure, and consumed before they are mutated. The read/edit split follows a precedent this document set already uses — `core/doc/Outline` is **○ (S3 read, S7 edit)** for the same reason. Building them at Stage 3 instead would force Stage 2 to invent a parallel geometry type and then delete it. | Stage 3 adds the mutating half of `PageTree` and everything else in `core/doc/` |
| **D2-6** | **An encrypted document is opened for READING through PDFium's password callback.** `PdfiumDocument::open(path, PasswordCb)` is the whole of Stage 2's encryption story; `pdf/cos/StandardSecurityHandler` stays Stage 10, and the COS reader **declines** an encrypted file with a named `Result` error rather than half-parsing it. | *"We own the crypt layer"* invites building it here, and the corpus needs an encrypted file this stage because the exit criteria say every corpus file opens. Two parsers reaching different conclusions about the same protected file is a class of bug that is very hard to see and very easy to create; one of them declining, by name and on purpose, is the honest arrangement until the handler exists. It also keeps the permission bits — which are a **security** decision (`core/doc/Permissions`, ○ S10) — out of a stage that has no command to refuse. | Stage 10, when the handler lands and the COS reader stops declining |
| **D2-7** | **`core/backend.h` declares all seven interfaces now — `IDocumentBackend`, `IPageRasterizer`, `ITextExtractor`, `MutationSink`, `ISigner`, `IOcrEngine`, `IIndexer` — and an unimplemented one returns an explicit not-implemented `Result`, never a silent success.** The backend reports which of the seven it implements, the way `platform::unimplementedInterfaces()` does. | Convention 6 (`CLAUDE.md`), generalized to the second interface header. That convention was written about `platform/` and its argument is not platform-specific: a stub that silently succeeds lets a caller believe a page was extracted or a signature applied, and that surfaces stages later as data nobody produced. Declaring all seven now is `Implementation-Plan.md`'s own instruction — *"so later stages add no boundary"* — and the not-implemented `Result` is what stops "declared" from meaning "quietly returns empty". | Never; each later stage implements its own and the census shrinks |
| **D2-8** | **`ui/viewer/PageView` is the first widget in the product to call `ShortcutBinder::declareContext()`.** It declares `KeyContext::Viewer`, and T14 registers the §8 Viewer commands. | Stage 1 shipped the entire context-resolution mechanism with **nothing declaring a context**, so `activeContext()` has answered `Global` for every keystroke the product has ever seen. That is the honest state, and it is also completely untested against a real second context: the twelve deliberate collisions in [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference) exist precisely so one chord means two things, and not one of them has ever resolved differently. Naming this as a decision makes it a **task with a case** rather than a side effect of building a widget — and the case that matters is the negative one: a chord bound in `Viewer` must **not** fire when focus is in the tool finder's search box. | Stage 4 (panes), Stage 13 (Write mode) each add their own; the mechanism is unchanged |
| **D2-9** | **The tile pipeline holds no PDFium type, and the engine is entered from ONE render thread.** ✅ **CORRECTED AT T11, as this row said it would be.** The original wording was *"`PdfiumRasterizer` is thread-confined per worker"*, and **that half was measured false at T3 (I-05)**. Per-worker confinement of the *document* is not confinement of the *engine*: what PDFium shares between threads is process-global (its font mapper and glyph caches), so eight workers with eight documents and eight pages corrupt the heap, and two are enough to do it. The first half stands unchanged and so does the `layer-lint` narrowing below. **T11 chose the replacement and it is ONE RENDER THREAD WITH A QUEUE.** The choice was made on T3's measurement rather than on taste: eight workers behind the process-wide engine lock retire **1.05×** the tiles per second of one worker, so the parallelism buys 5 %, while per-tile p95 is **0.532 ms against a 16.667 ms budget** — the engine is not the bottleneck, and 5 % of it is not worth a concurrency bug. One consumer also makes cancellation, which §5.3 calls *the feature*, provable rather than probable, and makes PDFium's single-threadedness structural rather than a discipline every future caller must remember. `RenderScheduler::Config::workerCount` remains and defaults to **1**, each worker taking its **own** `IPageRasterizer` from a factory, because T18 re-measures A2 through this pipeline and must be able to walk the count without a rebuild, and because a future non-PDFium rasterizer is not bound by I-05. D10's actual promise — *the UI thread never parses or rasterizes* — is kept, and strengthened: no subscriber is ever entered from a worker at all. `PdfiumInit` is the only translation unit that touches PDFium's global state, and the boundary carries **buffers and callbacks only**. `layer-lint`'s `FPDF_` rule narrows from `pdf/` to **`pdf/render/`**, with a fixture and a control, and gains a companion rule for the handle typedefs (`FPDF_DOCUMENT`, `FPDF_PAGE`, `FPDF_BITMAP`). | The existing rule permits `FPDF_` anywhere in `pdf/`, which was right when `pdf/` was one placeholder file and is too loose the moment `pdf/cos/` exists — the first-party COS layer is the half of D3 that must never learn about PDFium, and it lives inside the directory the rule currently allows. Narrowing it costs one `rules.json` entry and closes the gap while `pdf/cos/` is still empty, which is the only cheap moment. Per-worker confinement is `Implementation-Plan.md`'s stated design; making `PdfiumInit` the sole owner of global state is what makes it checkable. | Stage 5 adds `PdfiumTextExtractor` under the same rule; never relaxed |
| **D2-10** | **Stage 1's three carry-overs are Stage 2 tasks, not Stage 2 footnotes.** **I-53** — the *registered-but-currently-refused* tooltip — is built at T14 via the `tooltipKey` the registration already carries. **A-25**'s `--list-actions` switch is built at T18 (it is three lines over `ActionRegistry` and makes a diagnostic that six documents assume exists actually exist). **A-26**'s alias fixture is written at T18 against a synthetic `RAWPAGE_ACTION_ALIAS` row. | All three were carried explicitly rather than closed, and a carried item with no owning task is a deferral that becomes permanent. I-53 in particular *stops being theoretical at this stage* and the user's deferral said so: at Stage 1, 338 of 361 ids were unregistered so nearly every disabled control a person met was the explained kind — a real document makes commands **conditionally** available, which is the kind that says nothing today. A-26 matters more than its size: `RAWPAGE_ACTION_ALIAS` is live code with zero rows and no test, and the first stage to rename an id will be trusting a mechanism nothing has ever executed. | Never — this is a debt payment, not a policy |
| **D2-11** | **EVERY file in `corpus/` is generated by us. Nothing is borrowed, downloaded or curated — and `corpus/PROVENANCE.md` still gives every file a row**: which generator produced it, what it is for, and what is deliberately wrong with it. The generators are therefore held to a higher bar than “emit a text page”: they must produce **DCT, JPX and CCITT image streams** (encoded by the `codecs` libraries T7 turns on, from synthetic bitmaps), **shading patterns and transparency groups**, **vector line art with hairlines**, an **embedded CJK subset**, and a **form-heavy page** — because a construct the corpus never contains is a construct nothing in this project ever renders. | Decided by the user 2026-08-31, replacing an earlier generate-plus-curate split and **withdrawing precondition P-2** outright. Three things fall out. It keeps the repo self-contained and every file reproducible — `corpus/make-minimal.py` already records why that matters, since a PDF xref stores **absolute byte offsets** and a one-character edit invalidates every one of them. It means no committed file needs licence clearance, which is a gap **no gate in this project closes**: `license-audit` governs linked dependencies and says nothing about data. And for `corpus/malformed/` it is strictly better — *“the xref offsets are 12 bytes short from object 4 onward”* is a test, while *“this file is broken somehow”* is a mystery that gets re-diagnosed every time it fails. **The cost is real and is recorded rather than absorbed — see §12's first row and §13.6**: a generator emits only what its author already understood, so the golden baselines prove we render *our own output consistently*, not that we render *real-world PDFs correctly*. Real-producer constructs — a scanner's CCITT quirks, InDesign transparency, an unusual colour space, a broken-but-tolerated producer string — are **outside Stage 2's evidence entirely**, which is a different and weaker claim than the one an unqualified green golden suite implies. | **Stage 5 or the first real-world rendering bug, whichever comes first.** The moment a user's document renders wrong in a way the corpus cannot reproduce, this decision is what to revisit — and the answer then is a curated set with the licence pass P-2 was reserving |
| **D2-12** | **`RenderRequest` is a value type with no pointers and no handles, because its equality IS the cache key.** It carries page index, tile rect, device resolution, `Revision`, layer visibility and the render flags — and the `Revision` is mandatory rather than defaulted. | `Implementation-Plan.md` names the equality-is-the-key design; the consequence it does not spell out is that a single pointer member makes the key identity-based and the cache silently useless — every lookup misses, nothing fails, and the only symptom is that scrolling is slow for a reason no test can see. A defaulted `Revision` is the same failure with a worse ending: it makes a **stale tile after a Stage 3 mutation** the default behaviour. Both are cheap to prevent at declaration and expensive to find later. | Never |
| **D2-13** | **The golden comparison is perceptual with a stated threshold, and a failing case writes a diff image beside the expected and actual.** Baselines at 96/144/192 DPI. | An exact-match comparison over a rasterizer's output is a case that fails on things nobody cares about, and a suite that cries wolf is muted within a stage. The diff image is the half that makes a failure *actionable*: "golden mismatch, 0.7 % of pixels" is not a bug report, and Stage 1 spent five issues on the difference between a case that fails and a case that tells you what it saw. | Stage 5 and Stage 6 add their own baselines under the same tool |
| **D2-14** | **The disk tier of `RenderCache` is built behind the same interface as the memory tier, and O7 — shared between documents or per-document — is answered by MEASUREMENT over the corpus, not by design preference.** `performance/diskCacheMB` exists this stage; the tier may ship disabled by default if the measurement says so. | [`Implementation-Plan.md` §8](./Implementation-Plan.md#8-open-decisions-carried-forward) makes Stage 2 O7's owner and says the decision *"wants real cache-hit data from the corpus"*. That is only true if the instrumentation to produce that data is built with the cache rather than bolted on afterwards, so the plan names it here: hit rate, eviction rate and inter-document key overlap, reported by `bench_render`. Recording it now also prevents the failure mode where a stage owns a decision, ships, and the decision quietly stays open because nobody produced the number it was waiting for. | Stage 5, per O7's own row |

---

## 4. File manifest

Every file Stage 2 creates, with the [`Architecture-FileMap.md`](./Architecture-FileMap.md) row it must
flip **○ → ✅ (or 🟡) in the same change**. Rows marked ⚠️ have no FileMap row yet, or have one with the
wrong stage tag, and are lockstep debt — §10.

| File | FileMap row | Task |
|---|---|---|
| `core/backend.h` | §3.8 ✅ (S2 · T1) — row added with the header | T1 |
| `pdf/BackendFactory.{h,cpp}`, `pdf/DocumentBackendImpl.{h,cpp}` | §3.11 — both ✅ (S2 · T1, T9). **`BackendFactory.cpp` moved to `pdf/render/` at T9** (D2-2: the real backend is the engine, and the headless suite may not link it) | T1, T9 |
| `pdf/PdfModule.cpp` — **deleted** | §4 breadcrumb, **not a row** — a placeholder TU has no responsibility to fold anywhere, exactly as `platform/PlatformModule.cpp` was handled at Stage 1 T7 | T1 |
| `pdf/render/PdfiumInit.{h,cpp}` | §3.10 ✅ (S2 · T2) | T2 |
| `cmake/PdfiumAssert.cmake` | §3.18 ✅ (S2 · T2) — row added with the assertion | T2 |
| `tools/spike-pdfium/` — **deleted** | §3.17 ⤵ | T2 |
| `corpus/**`, `corpus/PROVENANCE.md`, `corpus/make-*.py` | §3.18 ✅ (S2 · T4) — twelve rows added: the provenance file, the driver, `pdfbuild.py`, the four encoders, `fontenc`, `cryptenc`, and one per generated directory | T4 |
| `pdf/cos/Lexer.{h,cpp}` | §3.9 ✅ (S2 · T5) | T5 |
| `pdf/cos/CosObject.{h,cpp}`, `pdf/cos/Parser.{h,cpp}` | §3.9 ✅ (S2 · T6) | T6 |
| `pdf/cos/XrefTable.{h,cpp}` | §3.9 ✅ (S2 · T7) | T7 |
| `pdf/cos/filters/*.{h,cpp}` — **nine** (D2-4) | §3.9 ✅ (S2 · T7); `Jbig2Filter` 🟡 declared-and-refusing (**I-06**), and the row's tenth filter re-tagged S10 | T7 |
| `pdf/cos/XrefRepair.{h,cpp}` | §3.9 ✅ (S2 · T8) | T8 |
| `core/doc/RepairReport.{h,cpp}` | §3.2 ✅ (S2 · T8) | T8 |
| `core/doc/Revision.h`, `core/doc/Scope.{h,cpp}` | §3.2 ✅ (S2 · T8) (D2-5) | T8 |
| `core/doc/Page.{h,cpp}` | §3.2 ✅ (S2 · T9) (D2-5) | T9 |
| `core/doc/PageTree.{h,cpp}` | §3.2 ✅ (S2 · T9, read half) (D2-5) | T9 |
| `pdf/render/PdfiumDocument.{h,cpp}`, `PdfiumRasterizer.{h,cpp}` | §3.10 ✅ (S2 · T9) | T9 |
| `core/render/DpiPolicy.{h,cpp}`, `TileGrid.{h,cpp}`, `PagePresentation.{h,cpp}` | §3.4 ✅ (S2 · T10) | T10 |
| `core/render/RenderRequest.h` | §3.4 ✅ **(S2 · T9, brought forward from T10)** — `IPageRasterizer::raster()` takes one and T9's exit criterion is a rasterized page | T9 |
| `core/render/RenderScheduler.{h,cpp}`, `RenderCache.{h,cpp}` | §3.4 ✅ (S2 · T11) | T11 |
| `core/render/DiskTileStore.{h,cpp}` | §3.4 ✅ **(S2 · T11)** — **a row this manifest did not list**, added with the file. D2-14 requires the disk tier to exist behind the same interface as the memory tier, and a tier whose I/O lived inside `RenderCache.cpp` could not be constructed against a temporary directory by a test, nor keep its two `layer-lint` write waivers away from the pure half | T11 |
| `platform/win/WinDisplayTopology.{h,cpp}`, `WinPowerSession.{h,cpp}` | §3.12 ✅ (S2 · T12) — the rows existed tagged ○ and the ○ → ✅ flip is what was owed | T12 |
| `platform/win/WinMessageWindow.{h,cpp}` | §3.12 ✅ **(S2 · T12)** — **a row this manifest did not list**, added with the file. Both T12 interfaces need the same hidden top-level window, because on Windows a notification IS a window message and neither class may own a `QWidget`; written twice, the second copy is where the missing `UnregisterClass` lives. It also carries the load-bearing note that a message-only window — the obvious choice — does **not** receive broadcasts and would register successfully and silently never fire. Same shape as `DiskTileStore` at T11 | T12 |
| `platform/win/WinFontResolver.{h,cpp}` | §3.12 ✅ (S2 · T16) — the fifth of the seventeen interfaces | T16 |
| `platform/win/WinAcceleratedSurface.{h,cpp}` | §3.12 ○ **(S4)** — **NOT built at T16, and re-tagged rather than left pending.** The guide's own caveat table has always put accelerated presentation at Stage 4 *if A2 says it is needed*, and A2's probe is p95 0.532 ms against a 16.667 ms budget, so the case for a GPU path is a measurement T18 has not made. The member stays null and the interface census asserts it | ~~T16~~ S4 |
| `ui/viewer/PageView.{h,cpp}`, `TilePresenter.{h,cpp}`, `PageLayoutEngine.{h,cpp}`, `ViewportController.{h,cpp}` | §3.14 ✅ (S2 · T13) | T13 |
| `app/BackendRasterizer.h` | §3.8 ✅ **(S2 · T13)** — **a second row this manifest did not list.** It was a class private to `CompositionRoot.cpp` and the consequence was issue **I-09**: the one arrangement the shipped product runs — the real engine, reached through the real backend, on a worker the real scheduler owns — was the one arrangement no suite could reach, because `tests/unit` and `tests/ui` may not link an engine (D2-2, D1-4) and `tests/render` had no scheduler. As a header naming only `core/` types, case **A-79** drives the shipped class rather than a copy | T13 |
| `ui/shell/DocumentWindow.{h,cpp}` — **the D1-2 stub replaced** | §3.13 🟡 (S1 T12 stub, S2 · T13 the viewer) — **not a new file and not in this manifest**, because the plan's §2 ledger already said T13 replaces its placeholder canvas and *"the emptiness was the feature; do not preserve it"* | T13 |
| `ui/shell/MessageBar.{h,cpp}` | §3.13 ✅ (S2 · T14) | T14 |
| `ui/dialogs/PasswordDialog.{h,cpp}` | §3.13 ✅ **(S2 · T14)** — **a third row this manifest did not list.** D2-6 makes the password callback the whole of Stage 2's encryption story and this plan grouped "the password prompt" with the message bar; building it found they are different surfaces answering different questions — the prompt is modal by necessity because `open()` cannot continue without it, the banner is a persistent statement about a document that is already open | T14 |
| `pdf/render/GlyphCache.{h,cpp}`, `FontResolverBridge.{h,cpp}` | §3.10 ✅ (S2 · T16) — and `GlyphCache` is **not** what its name says: PDFium caches glyphs, this caches the FACE DATA behind the substitution hook, which is the correction to guide case A-67 | T16 |
| `core/render/FontSubstitution.{h,cpp}` | §3.4 ✅ **(S2 · T16)** — **a row this manifest did not list**, added with the file on the precedent `DiskTileStore` set at T11. WHICH face substitutes is policy and `platform/win/` makes none, so the ordered candidate list is in `core/` — which is also what makes it assertable with no display, no Windows and no engine. Fourth such file this stage after `WinMessageWindow` (T12), `app/BackendRasterizer.h` (T13) and `PasswordDialog` (T14) | T16 |
| `corpus/ordinary/font-not-embedded.pdf` | §3.18 ✅ **(S2 · T16)** — **a corpus file this manifest did not list**, and A-66 needs it: every other text fixture uses a base-14 face or an embedded `/FontFile2`, and neither reaches a font resolver | T16 |
| `tests/golden/`, `tools/golden-update/` | §3.17 ✅ **(S2 · T17)** — both rows flipped ○ → ✅ in the same change as the code, and the ⚠️ this row carried (*“the tool does not have one”*) was already stale when it was written: re-measured 2026-09-02, §3.17 has had a row for `tools/golden-update/` all along. T17 also added **five data rows** the manifest did not list — `golden/manifest.json`, `golden/captured.json`, `golden/README.md`, `golden/UPDATES.md` and `golden/**` — on the `WinMessageWindow` / `FontSubstitution` precedent | T17 |
| `tests/bench/bench_render.cpp` | §3.17 🟡 (S2 · T3; T18 completes it) | T3, T18 |
| `tests/fuzz/` | §3.17 ✅ (S2 · T5, T6) — the stage retag was made when this plan was written | T5, T6 |

**Settings keys** — the sixteen from
[`Implementation-Plan.md` § Stage 2](./Implementation-Plan.md#stage-2--pdf-engine-boundary-and-tiled-rendering)
(eleven `pageDisplay/`, five `performance/`), each a row in `core/settings/SettingsKeys.def` and
therefore, by Stage 1 convention 17, an automatic Preferences editor with no widget code. Two new
Preferences groups appear — **Page Display** and **Performance** — taking the shipped count from
three groups to five of 26.

---

## 5. Component design

Only the pieces whose shape is not obvious from a one-line component entry.

### 5.1 The library split and what it protects

```
rawpage_core        core/ …                      portable, no Qt GUI, no engine
rawpage_cos         pdf/cos/ + pdf/BackendFactory  portable, FIRST-PARTY, no PDFium   ← NEW
rawpage_pdfium      pdf/render/                    Windows, links pdfium.dll          ← NEW
rawpage_platform    platform/win/                  Windows
rawpage_ui          ui/                            Qt Widgets
```

`rawpage_tests` links `rawpage_core` + `rawpage_cos` and calls `rawpage_assert_headless()`, whose
forbidden list becomes `rawpage_ui`, `rawpage_platform`, **`rawpage_pdfium`**. The portable presets
build `core/` + `pdf/cos/` exactly as `Tech-Stack-Plan.md` §6.3 says they do — which is a sentence
that has been true only because `pdf/` was empty.

A fourth test target arrives, `rawpage_render_tests` (`render.` prefix), linking `rawpage_core` +
`rawpage_cos` + `rawpage_pdfium`: **Windows only**, because it rasterizes. The reasoning is D1-4's,
from a third side — the raster suite is precisely the pressure that would otherwise add
`rawpage_pdfium` to `rawpage_tests` and destroy the property the split just created.

### 5.2 `DpiPolicy` — the file most likely to cause a blurry-text bug

Three inputs (zoom, monitor scale, monitor physical DPI) and two outputs:

- `Resolution resolve(Zoom, MonitorScale, PhysicalDpi) const` — the device resolution a tile is
  rasterized at. **Actual Size means 100 % at the monitor's physical DPI**, per
  [`Features.md` §1.2](./Features.md#12-page-display), not 100 % of a logical pixel.
- `bool requiresRerender(Resolution, Resolution) const` — the distinction the whole stage rests on.
  A scale change that crosses the threshold needs re-rasterization; one that does not is a cheap
  resample of tiles already in hand. Getting it wrong in one direction gives blurry text on every
  monitor move, and in the other a stutter on every one.

Pure, and therefore tested in `rawpage_tests` with no display — including the pathological cases a
real rig cannot produce on demand: 8 %, 6400 %, a 350 % monitor scale, and a scale change of one
step in each direction across the threshold.

### 5.3 `RenderScheduler` — cancellation is the feature

`Handle enqueue(RenderRequest, Priority)` · `void cancel(Handle)` · `Signal<RenderRequest, QImage> ready`.

Three properties, each with its own case, and the third is the one that is hard:

1. A request whose tile has left the viewport is **removed from the queue**, and if already running is
   abandoned on delivery rather than cached. A test asserts the worker never *started* it.
2. Duplicate requests coalesce — two viewports over one tile is one rasterization.
3. **Delivery is on the UI thread, and a delivery for a cancelled or superseded request is dropped.**
   This is where a use-after-free lives: a `PageView` closed while four of its tiles are in flight.
   Convention 10 (`CLAUDE.md`) is the Stage 1 form of the same bug and cost an S1 segfault there.

The pool size comes from `performance/renderWorkers`; `platform::IPowerSession` throttles it when
`performance/throttleOnBattery` is set and the machine is on battery.

### 5.4 `XrefRepair` and the report it owes Stage 3

A full-file object scan when the xref is unusable, rebuilding the table and emitting a
`core::RepairReport` — per object, with severity. Two consumers, one of them not yet built:

- **Now:** `ui/shell/MessageBar` says the file was repaired and offers the detail.
- **Stage 3:** a repaired document's first save is **forced to Save-As**
  ([`CLAUDE.md`](../CLAUDE.md), standing conventions). Stage 2 does not implement that, and the
  report must carry enough for Stage 3 to enforce it — a boolean is not enough, because
  *"reconstructed 3 objects of 4,000"* and *"reconstructed the entire xref"* are different risks.

`Result<T>` at the boundary, never an exception: `pdf/cos/Parser` returning a named error is how a
malformed file becomes a message rather than a crash, and the corpus's whole malformed set exists to
prove it. **Every `corpus/malformed/` file yields a report and never a crash** is a Stage 2 exit
criterion, and the fuzz targets seeded from that directory are the same claim made continuously.

### 5.5 `PagePresentation` — four layout modes and the two that are not symmetric

`QVector<QRectF> layout(const PageTree&, LayoutMode, Zoom) const`, over Single, Single Continuous,
Two-Up and Two-Up Continuous, plus `pageDisplay/showCoverInTwoUp` and `pageDisplay/rtlBinding`.

Pure, so all of it is tested headless. Two asymmetries are worth naming because they are where the
off-by-one lives: a **cover page** shifts every subsequent pair's parity, and **RTL binding** swaps
which side of the spread page *n* lands on without changing the page order. A document with an odd
page count, a cover page, and RTL binding exercises all three at once, and belongs in the corpus.

### 5.6 Test targets after Stage 2

| Target | Links | Suites | Runs where |
|---|---|---|---|
| `rawpage_tests` | `rawpage_core`, **`rawpage_cos`** | `tst_dpipolicy`, `tst_tilegrid`, `tst_rendercache`, `tst_presentation`, `tst_cos_lexer`, `tst_cos_parser`, `tst_cos_filters`, `tst_xrefrepair`, `tst_pagegeometry`, plus every Stage 0/1 suite | Windows + `portable-asan` |
| `rawpage_render_tests` (**new**, §5.1) | + `rawpage_pdfium` | `tst_pdfiumdoc`, `tst_rasterizer`, `tst_golden` | **Windows only** |
| `rawpage_ui_tests` | `rawpage_core`, `rawpage_ui` | + `tst_pageview`, `tst_viewport`, `tst_viewercontext` | **Windows only**, `offscreen` |
| `rawpage_platform_tests` | `rawpage_core`, `rawpage_platform` | + `tst_displaytopology` | **Windows only** |
| `rawpage_bench_render` (**new**) | `rawpage_core`, `rawpage_cos`, `rawpage_pdfium` | A2's figure, and O7's cache statistics (D2-14) | **Windows only**. A benchmark **reports**; its exit code is about whether the measurement was possible, never whether the machine was fast |
| `tests/fuzz/` | `rawpage_cos` | `fuzz_cos_lexer`, `fuzz_cos_parser` | **`portable-fuzz` only** (Linux, clang) |

---

## 6. Task breakdown

Ordering is risk-first, and **T3 comes before every line of tiling, scheduling or viewer code** —
a no-go on A2 reshapes T10 and T11 completely, and learning it after T13 is a rewrite. This is
Stage 1 T2's ordering argument applied to the second go/no-go.

| # | Task | Depends on | Content | Done when |
|---|---|---|---|---|
| **T1** | The engine boundary and the library split | — | `core/backend.h` with all seven interfaces (D2-7); `pdf/BackendFactory` returning a backend that implements none of them yet and says so; the `rawpage_cos` / `rawpage_pdfium` split with the strengthened `rawpage_assert_headless()` (D2-2); `pdf/PdfModule.cpp` **deleted** | `ctest` still 354/354; `portable-asan` still configures and runs; a deliberate `rawpage_pdfium` edge added to `rawpage_tests` **fails configure**, proven by a fixture |
| **T2** | PDFium made permanent | T1 | `pdf/render/PdfiumInit`; `cmake/PdfiumAssert.cmake` pinning 153.0.8009.0 with V8 and XFA off (D2-3); `pdfium.dll` deployed beside the executable; `license-audit` sees BSD-3; `layer-lint`'s `FPDF_` rule narrowed to `pdf/render/` with a fixture and a control (D2-9); **`tools/spike-pdfium/` deleted** | `rawpage.exe --version` reports the PDFium version; a mismatched `VERSION` fails configure with a named diagnostic; `checks` clean |
| **T3** | **A2 early probe — the go/no-go** | T2 | A throwaway harness over raw `FPDF_RenderPageBitmap` at the target device resolution, p50/p95 over a text-heavy A4 page at 200 % on the 150 %-scaled monitor, 8 workers (D2-1, first half) | **A2 verdict recorded** in `Tech-Stack-Plan.md` §3.3 with numbers and the machine it was measured on. A no-go stops the stage and reopens D10 before T10 |
| **T4** | The corpus — **entirely generated** | — | `corpus/` per D2-11, **every file produced by a script we own**: ~20 ordinary files for the A2 measurement and the golden baselines, carrying between them DCT/JPX/CCITT image streams, shading patterns, transparency groups, hairline vector art, an embedded CJK subset and a form-heavy page; `corpus/malformed/` with each damage described exactly; encrypted, tagged, 900-page, and odd-count-plus-cover-plus-RTL files; `corpus/PROVENANCE.md` | Every file has a provenance row naming its generator and its purpose; **every file regenerates byte-identically from a clean checkout**, `corpus/minimal.pdf` included — that reproducibility is the property the whole decision rests on, and it is asserted rather than assumed |
| **T5** | `pdf/cos/Lexer` | T1 | Tokenizer: delimiters, literal and hex strings, numbers, names, comments. Tolerant by design | `tst_cos_lexer` green; `fuzz_cos_lexer` builds on `portable-fuzz` and runs 10 minutes clean — **the preset's first real target** |
| **T6** | `pdf/cos/CosObject` + `Parser` | T5 | The object variant with identity-preserving round-trip of anything we do not interpret; indirect objects; object streams; **length recovery when `/Length` lies** | `tst_cos_parser` green; `fuzz_cos_parser` clean; a `/Length` that lies is recovered, with a control proving the recovery path was taken |
| **T7** | `XrefTable` + the nine filters | T6 | Classic tables, xref streams, the `/Prev` chain across incremental updates, the trailer; `Flate`, `LZW`, `Ascii85`, `AsciiHex`, `RunLength`, `Dct`, `Jpx`, `Ccitt`, `Jbig2` — **not `CryptFilter`** (D2-4). vcpkg's `codecs` feature turned on, **and `spdx-map.json` gains the transitive ports that arrive with it** (I-02) | `tst_cos_filters` — each filter round-trips; `tst_xreftable` walks a three-generation incremental file; **`license-audit` green over the resolved closure, not just the manifest** |
| **T8** | `XrefRepair`, `RepairReport`, `Revision`, `Scope` | T7 | §5.4; plus the two `core/doc/` vocabulary types D2-5 brings forward | **Every `corpus/malformed/` file yields a report and never a crash**; `portable-asan` parses the whole corpus clean under ASan+UBSan |
| **T9** | A document opens | T2, T8 | `PdfiumDocument` (with `PasswordCb`, D2-6), `PdfiumRasterizer`, `DocumentBackendImpl`; `core/doc/Page` and read-only `PageTree` (D2-5); `MainWindow::openFiles()` calls the backend instead of checking a suffix | A corpus file opens and one page rasterizes to a `QImage` in a test. **`DocumentWindow` stops being a stub** |
| **T10** | `core/render/` policy | T3, T9 | `DpiPolicy` (§5.2), `TileGrid`, `RenderRequest` (D2-12), `PagePresentation` (§5.5) | Four pure suites green, including 8 %, 6400 %, cover-page parity and RTL binding |
| **T11** | `RenderScheduler` + `RenderCache` | T10 | §5.3; the worker pool; memory and disk tiers with O7's instrumentation (D2-14) | Cancellation asserted by the worker never starting; `tst_rendercache` covers LRU, the cap, and `Scope`/`Revision` invalidation |
| **T12** | `WinDisplayTopology`, `WinPowerSession` | T11 | `monitors()`, `Signal<> changed`; battery and session-lock state | `tst_displaytopology` reports **both** monitors with their real scale factors on the P-1 rig; the `changed` signal fires on a mid-session scale change |
| **T13** | `ui/viewer/` | T11, T12 | `PageView`, `TilePresenter`, `PageLayoutEngine`, `ViewportController`; `declareContext(KeyContext::Viewer)` (D2-8) | **A page is on screen.** `tst_pageview` asserts the geometry the layout gave the widget *and then* the tiles painted into it (§1, I-36's lesson) |
| **T14** | The Viewer commands, the status bar, the message bar | T13 | The §8 Viewer bindings registered; `StatusBar` gets real page numbers and zoom; `MessageBar` with the repair report and the password prompt; **I-53's `tooltipKey`** (D2-10) | Every §8 Viewer chord dispatches; a `Viewer` chord does **not** fire from the tool finder's search box; a disabled registered command says *why* |
| **T15** | The sixteen settings keys | T14 | Eleven `pageDisplay/`, five `performance/`, each a `SettingsKeys.def` row and therefore an automatic editor (convention 17); two new Preferences groups | Changing anti-aliasing, line weight or the resolution cap visibly changes rendering; every key round-trips |
| **T16** | Fonts and glyphs | T13 | ✅ **2026-09-05.** `GlyphCache`, `FontResolverBridge`, `WinFontResolver`, plus `core/render/FontSubstitution` and a thirtieth corpus file, neither of which this plan listed. **The `text` feature was SPLIT rather than turned on** — see the deviation below and issue **I-12** — and I-02's Stage 2 half is closed by the `objdump` measurement rather than by a source offer. **I-04 closed** as well, with the `unknown-key` rule and the eighteen keys it found | ✅ Met, and the substitute is NAMED: `corpus/ordinary/font-not-embedded.pdf` resolves its serif font to **Times New Roman** and its sans to **Segoe UI**, through `FontResolverBridge` → `WinFontResolver` → `FontSubstitution`. `objdump -p` on the built `rawpage.exe` names **eighteen** DLLs — seventeen from before plus `DWrite.dll`, an OS library — and **not one** comes from the `text` ports |
| **T17** | Golden images | T13, T16 | ✅ **2026-09-06.** `tests/golden/` at 96/144/192 DPI over the corpus — **198 baselines, 56 files, 66 pages**, a **fifth test target** because the PNG codec needs QtGui and `tests/render` deliberately links no Qt GUI library; `tools/golden-update` with its mandatory `--reason` (D2-3, D2-13) plus `--survey`, `--check-manifest` and `--explain`; a diff image on failure. `golden/manifest.json` declares, `golden/captured.json` measures | ✅ Baselines committed; a deliberately perturbed render fails **with a diff image**; `golden-update` without `--reason` refuses with **exit 2** and a diagnostic citing D2-3. **Two findings, both S3 and both in T17's own harness** — I-15 (a perturbation control aimed at a page the perturbation could not change) and I-14 (a capture window that recorded no font substitutions for the one file whose purpose is to have them, silently) |
| **T18** | **A2 re-measured**, carry-overs, CI, doc lockstep | all | ✅ **2026-09-06.** `tests/bench/bench_pipeline` — a SECOND binary, not a rewrite of `bench_render`, because the probe's figure must stay reproducible and `--unsafe-parallel` is I-05's reproducer — driving the shipped scheduler and cache over the whole corpus; **O7 and O10 resolved**; **D2-14 answered** (the disk tier does not ship); `--list-actions` and `tests/unit/tst_actionalias.cpp` for D2-10's two carry-overs; **`render` added to CI's non-empty-prefix loop** and a **`portable-fuzz` job** added, which is what finally gives A-25 and A-26 results; §10's debt paid. **A2 = `go`, p95 14.18 ms against 16.667 ms — 1.18× headroom, against the probe's 31×, and the gap between those two numbers is the whole reason D2-1 asked for both.** | ~~`bench_render` through the **shipped** pipeline (D2-1, second half); O7 and O10 resolved in `Implementation-Plan.md` §8; `--list-actions` and the alias fixture (D2-10); CI gains `render.` and the fuzz smoke run; every §4 row flipped and §10's debt paid~~ — all done. **One thing in this row was ambiguous and both readings turned out to be owed: “A-25 and A-26”.** D2-10's are **Stage 1's** (the `--list-actions` switch and the alias fixture); the *Done when* column's are naturally read as **Stage 2's** (the two fuzz targets). All four were addressed, which is luck rather than clarity — the collision `CLAUDE.md`'s *Citing a label* section warns about, in this document's own task table. | ✅ The product's own A2 figure recorded beside the probe's; **all four A-25/A-26 cases have results** — Stage 1's two ✅ and Stage 2's two 🟡 (they build and run seeded on every push; the ten-minute soak is Stage 3's); `Implementation-Plan.md` Stage 2 status block reflects reality, deviations included |

---

## 7. Blocking preconditions — setup checklist

| # | Precondition | Why | State |
|---|---|---|---|
| **P-1** | **Two monitors at different scale factors**, at least one changeable while the app runs | A2's specification, and manual cases M-01 and M-02. The mixed-DPI drag and the mid-session scale change are the two things no offscreen run can reach | ✅ **Satisfied, and it matches A2's specification exactly** — measured 2026-08-31: a 4K panel (3840×2160 physical, 2560×1440 logical = **150 %**) plus a 2560×1440 at **100 %**; 16 cores / 32 threads, so an 8-worker pool is real rather than notional. **The guide re-asserts this through the product's own `WinDisplayTopology`** (T12) rather than trusting the measurement, because a DPI-unaware query reports virtualized figures and would agree with itself while being wrong |
| ~~**P-2**~~ | ⤵ **Withdrawn 2026-08-31** — *licence clearance for the curated corpus files.* **D2-11 now generates the entire corpus**, so nothing is borrowed and there is nothing to clear. The row is kept struck through rather than deleted, per this document set's convention, so a later citation still resolves and so a reader can see the precondition was **removed by decision** rather than forgotten. | — | ⤵ **No longer required.** If a curated set is ever wanted — D2-11's *Revisit* column says when — this is the precondition to reinstate first, and its four questions per file are still the right ones |
| **P-3** | **`vcpkg.json`'s `codecs` and `text` features resolve on `x64-mingw-dynamic`** — and `text` **became three features at T16**: `fonts`, `shaping`, `spelling`, all three dry-run clean, none of them turned on by Stage 2 (issue **I-12**) | Declared at Stage 0 and **never built** — `zlib`, `libjpeg-turbo`, `openjpeg`, `tiff`, `libpng`, `freetype`, `harfbuzz`, `icu`, `hunspell`. Stage 0 deliberately kept them dry-run-only so risk A5 could be answered without coupling the build; T7 and T16 are where the answer stops being theoretical | ✅ **Satisfied — dry-run 2026-08-31, and BUILT FOR REAL at T7 on 2026-09-04**: `vcpkg install --x-feature=codecs` on `x64-mingw-dynamic` completed in **1.2 minutes**, all five ports, and `CMakePresets.json` now sets `VCPKG_MANIFEST_FEATURES=tests;codecs` for the Windows **and portable** presets — so `pdf/cos/` links zlib, libjpeg-turbo and openjpeg on Linux too, and the first `portable-asan` run after T7 is the first evidence that `x64-linux` resolution works. The original dry-run: `vcpkg install --dry-run` on `x64-mingw-dynamic`, host and target, **exit 0 for `codecs`, for `text`, and for both together**; vcpkg detected Qt's own `x86_64-w64-mingw32-g++` as the compiler, so the resolution was against the pinned toolchain rather than a stray one. All nine declared ports resolve. **The dry-run also produced a finding that is not about resolution — see I-02 in the guide's §8**: the closure pulls in four ports `vcpkg.json` never names (`brotli`, `bzip2`, `libiconv`, `liblzma`), and `license-audit` cannot see them, because it inventories the **declared manifest** rather than the **resolved closure**. `libiconv` is the one that matters: GNU libiconv is **LGPL-2.1-or-later** and carries a source-offer obligation nobody has recorded |
| **P-4** | **A reference renderer whose engine is INDEPENDENT of PDFium** — owner: **you**. Adobe Acrobat Reader is the primary; **SumatraPDF (MuPDF)** and **Firefox (pdf.js)** are secondary cross-checks. **Microsoft Edge and Google Chrome do NOT qualify and must not be used** | Manual cases **M-03** (rasterization fidelity) and **M-05** (layout modes, cover page, RTL). “Looks fine” is not a result, and a rasterization difference is only visible beside something else. **The independence clause is the whole point and this row originally omitted it**: Edge and Chrome ship on essentially every Windows machine and both render PDFs *through PDFium* — the same library `third_party/pdfium` comes from — so a comparison against either is our PDFium against their PDFium. It would agree perfectly and mean nothing, and it would do so in **the one case that exists to catch what the golden suite structurally cannot** (§1 of the guide, *What this corpus cannot tell us*). With the corpus now entirely generated, M-03 is the only independent evidence Stage 2 has about rasterization at all | ✅ **Satisfied — 2026-08-31.** **Adobe Acrobat (64-bit) 26.001.21771**, publisher Adobe, installed by the user from the official installer at `C:\Program Files\Adobe\Acrobat DC\` — the unified Acrobat binary, whose renderer is Adobe's own either way, so it serves as the primary reference whether it is entitled as Reader or as Pro. Two independent secondaries alongside it: **SumatraPDF 3.5.2** (MuPDF) and **Firefox 154** (pdf.js), giving M-03 three engines that share nothing with PDFium. Still **disqualified**: Edge 152 and Chrome 152. **The installer did NOT claim the `.pdf` association** — re-checked after installation, `UserChoice` is still `Applications\rawpage.exe` from Stage 1's M-07 walk — so a reference must be opened via right-click → *Open with*, which the guide's rig table and M-03's *Pre* both say. **The earlier claim in this row that Stage 1's P-4 had already established the rig was wrong**: Stage 1's P-4 was *“Right PDF Professional **or Office**”*, for judging whether a ribbon looks like a ribbon, and Office renders no PDFs at all |

**Stage 2 has NO open preconditions.** P-1 and P-3 were measured and satisfied on 2026-08-31, P-2 was
withdrawn the same day with D2-11's all-generated corpus, and P-4 was satisfied when the user installed
Acrobat. Every task from T1 onward proceeds, and every manual case has the rig it needs. **That is worth
stating rather than leaving implicit**: Stage 1 discovered at M-07 that a case can be blocked by an
unstated precondition nobody had written down (its **I-46**), and a table where every row is closed is
the evidence that did not happen again here.

**A reference matters MORE now, not less, because the corpus is entirely ours.** Comparing two
renderers over the same generated file still catches a wrong glyph weight, a wrong colour or missing
content — and with `tests/golden/` reduced to a consistency check against our own output (D2-11,
§12's first row), M-03 is the only place in Stage 2 where an **independent** implementation looks at
the same bytes we do. That is exactly why the engine-independence clause is load-bearing rather than
pedantic: a reference sharing our engine would turn the stage's one independent check into a second
copy of the circular one.

---

## 8. Verdict ledger — what Stage 2 owes

A verdict is not recorded in this file. Each goes to its **owning document**, in the same change as
the work that produced it.

| Verdict | Produced by | Recorded in |
|---|---|---|
| **A2** — is PDFium's raster throughput sufficient for smooth scrolling? | **T3** (the probe, early) and **T18** (the shipped pipeline, final) — D2-1 | [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified), beside A3, A5 and A6; summarized in the Stage 2 status block. **Both figures, the second labelled as the product's** |
| **O7** — is the render cache's disk tier shared between documents or per-document? | T11's instrumentation, read at T18 (D2-14) | [`Implementation-Plan.md` §8](./Implementation-Plan.md#8-open-decisions-carried-forward), struck through and dated, per that table's convention |
| **O10** — how is sanitizer coverage obtained, given MinGW GCC has none? | T8's `portable-asan` corpus run and T5/T6's first real `portable-fuzz` targets | `Implementation-Plan.md` §8. Stage 2 is its owner stage, and the portable route stops being *the plan of record* and becomes *the measured answer* — including what it demonstrably cannot reach, which is `platform/win/` and `ui/` |

**A stage that is green but has no verdicts is not signed off.** Stage 2 owes three.

---

## 9. CI changes

| Job | Before | After |
|---|---|---|
| `gates` (ubuntu, no Qt) | 66 cases, three gates | Plus `layer-lint`'s narrowed `FPDF_` rule and the handle rule, each with a fixture **and a control** (D2-9); plus the `PdfiumAssert` fixture. Still three gates, still no Qt, still runs before any compile |
| `windows` (self-hosted) | 354 tests | Plus `rawpage_tests`' eight new suites, **`rawpage_render_tests`** (new), the `ui.` viewer suites, `tst_displaytopology`, and `tests/golden/`. The T15-era assertion that a suite prefix is never *empty* extends to `render.` — the whole point of that assertion is that a target which failed to build subtracts cases from a run still reporting *"100% tests passed"* |
| `portable-asan` (ubuntu) | 196 tests | Plus the COS parse of the **whole corpus** under ASan+UBSan. This is the job that makes D2-2's split worth its cost, and it is also the one that would have silently stopped configuring without it |
| `portable-fuzz` (ubuntu, clang) | **Never run — both targets now EXIST (T5, T6) and neither has been linked or run; there is no clang on the dev box** | `fuzz_cos_lexer` and `fuzz_cos_parser`, seeded from `corpus/malformed/`, 10 minutes clean in CI. **A preset that has never built anything has not been proven to build anything** — Stage 1's I-30, where a job everyone assumed was running had never run, is the argument for treating the first green run as a finding rather than a formality |
| `coverage-report` | Names what went unexercised when a job did not run | Gains the new prefixes, so a skipped `render.` cannot be misread as coverage |

**The raster path cannot leave Windows** — PDFium is in it — so ASan does not cover it. Per
[`Implementation-Plan.md` § Stage 2](./Implementation-Plan.md#stage-2--pdf-engine-boundary-and-tiled-rendering)
and risk **R7**, it is checked with Dr. Memory / Application Verifier over the same corpus instead,
and that run is a **manual** case rather than a CI job because neither tool is installed on the
runner. Its absence from CI is stated here so it is a known gap rather than an assumed cover.

---

## 10. Lockstep debt this stage creates

Owed by **T18**, in the same change as the code — except the two marked *now*, which are corrections
to existing documents and are made when this plan is written.

- ~~**⚠️ NOW — `Architecture-FileMap.md` §3.9: the six `pdf/cos/` reader rows (fifteen files) are tagged
  ○ (S3) and belong to Stage 2.**~~ **Paid when this plan was written; `Lexer` flipped ○ → ✅ at T5.** `Implementation-Plan.md` § Stage 2 lists `CosObject`, `Lexer`, `Parser`,
  `XrefTable`, `XrefRepair` and the filters as Stage 2 components, and Stage 3's own component list
  claims only `IncrementalWriter` and `ObjectAllocator`. `Implementation-Plan.md` owns stage
  ordering, so the FileMap is the document that is wrong. Confirmed with the user 2026-08-31.
- ~~**⚠️ NOW — `Architecture-FileMap.md` §3.2: `Revision.h`, `Scope` and `Page` are tagged ○ (S3) while
  their own *Interacts with* column names Stage 2 consumers**~~ **— paid: all four now read ○ (S2), with `PageTree` as ○ (S2 read, S3 edit)**, and `PageTree` needs the split tag
  `○ (S2 read, S3 edit)`. See **D2-5**.
- ~~**⚠️ NOW — `Architecture-FileMap.md` §3.17 and `Implementation-Plan.md` § Stage 3: `tests/fuzz/` was
  tagged ○ (S3) and listed among Stage 3's components**~~ **— paid, and the row went 🟡 (S2 · T5) when `fuzz_cos_lexer` made it real**, though a fuzz target over `pdf/cos/Lexer` and
  `Parser` cannot exist before those files do — and `CMakePresets.json`'s `portable-fuzz` description
  has said *“NO TARGETS EXIST until Stage 2”* since Stage 0. **The build system was right and both
  documents were wrong**, for the same root cause as the two entries above: they were written when the
  COS reader was believed to be Stage 3. Moved to Stage 2 T5/T6 in the same change; Stage 3's row is
  struck through rather than deleted, and Stage 3 keeps the *seeds*.
- **`tools/license-audit/spdx-map.json` needs entries for `brotli`, `bzip2`, `libiconv` and
  `liblzma`, and `Tech-Stack-Plan.md` §8.2's allowlist needs `bzip2-1.0.6` and `0BSD`** — measured
  2026-08-31 by P-3's dry-run and **verified against upstream licence text the same day**, recorded as
  **I-02**. Confirmed: `brotli` **MIT** (already allowlisted), `bzip2` **`bzip2-1.0.6`**, `liblzma`
  **`0BSD`** — all three permissive with no copyleft. The deeper half is that `license-audit` audits
  the **declared** manifest, not the **resolved** closure, so it cannot currently see any of them; the
  allowlist rows are the easy part and the inventory change is the real one.
- ~~**`libiconv`'s only parent is `hunspell`, not `icu`… `vcpkg.json`'s `text` feature is too coarse
  for T16.**~~ **PAID AT T16, 2026-09-05, by taking the FIRST of the two options this item offered:
  the feature is SPLIT.** `text` is now `fonts` (freetype), `shaping` (harfbuzz + icu) and `spelling`
  (hunspell), each named for its own stage, and all three dry-run clean on `x64-mingw-dynamic`.
  Splitting rather than recording is what makes the hazard structural: `fonts` + `shaping` together
  pull **neither hunspell nor libiconv** — measured, not asserted — so Stage 13 turns `spelling` on and
  nothing before it puts a GPL `iconv.exe` into the vcpkg tree eleven stages before anything links the
  library.
  **And Stage 2 turns on NONE of the three**, which is a deviation from T16's *Done when* and is issue
  **I-12** in the guide. The clause said *“vcpkg's `text` feature turned on”*; the measurement that
  decides it was in the same clause, and it says the opposite. PDFium bundles its own FreeType and T16's
  substitution reaches the OS through DirectWrite, handing the engine the face's BYTES — so Stage 2
  links nothing from any of them. `objdump -p` on the built `rawpage.exe`: **eighteen DLLs, and not one
  of them is freetype, brotli, bzip2, icu, harfbuzz, hunspell or libiconv.** Building four ports we do
  not link, to satisfy a clause whose own evidence says not to, is the shape of work that gets done
  because a document asked for it. **`Tech-Stack-Plan.md` §6.2 already said Stage 5 for `text`**, which
  is the owning document and was right.
- **`libiconv` is a SPLIT package and needs two separate decisions, not one row.** vcpkg declares its
  licence `null` and its portfile says why: *the libiconv and libcharset **libraries** are LGPL; the
  **iconv program** and the documentation are GPL.* (a) **What we link is LGPL-2.1-or-later** — D4
  permits it, and it needs the same treatment Qt and Hunspell already have in `spdx-map.json`: a
  recorded election, dynamic linking, a user-replaceable binary, and a **corresponding-source offer**,
  which does not exist today. (b) **What we must never ship is the GPL `iconv.exe`**, which the port
  *does* install into `installed/<triplet>/tools/libiconv/bin/`. Harmless at Stage 2 — a build-tree
  artifact, and D4 forbids a GPL *dependency* rather than a GPL binary in a toolchain directory — and
  a real hazard at **Stage 15**, where MSI/MSIX packaging decides what gets copied — and it is a hazard
  whether or not we ever link the library, because the tool is installed by the port rather than by the
  link line. **Flagged forward
  explicitly, because a packaging stage that sweeps `tools/` will not rediscover this on its own.**
- ~~**`corpus/PROVENANCE.md` and T4's new generators have no FileMap rows.**~~ **Paid at T4,
  2026-09-02.** Twelve rows added to §3.18 — `PROVENANCE.md`, the driver, `pdfbuild.py`, the four
  encoder modules (`deflate`, `jpegenc`, `ccitt`, `jpxenc`), `fontenc.py`, `cryptenc.py`, and one per
  generated directory. The `.gitattributes` row was amended in the same change: `corpus/** binary` was
  marking the new generators binary too, so `git diff` showed nothing for them.
- ~~**`core/backend.h` has no FileMap row.**~~ **Paid at T1**, with the `core-backend` `layer-lint`
  entry the item below anticipated. §3.8 covers `core/batch/`, `core/settings/` and shared
  headers; the second of the two interface headers this architecture rests on is not in it, though
  `platform/platform.h` is. Add it, with the `layer-lint` layer entry it will need for the same
  reason `platform/platform.h` has one — `core/`, `pdf/` and `app/` all include it.
- ~~🟡 **`pdf/BackendFactory` has a row as of T1; `pdf/DocumentBackendImpl` does not exist yet and is T9's.**~~ **Paid — re-read 2026-09-14:** `Architecture-FileMap.md` §3 has ✅ rows for `pdf/DocumentBackendImpl.{h,cpp}` (T9) and `pdf/BackendFactory.h` with its `.cpp` moved to `pdf/render/` (T9), and `PdfModule.cpp`'s deletion is recorded. The item was left 🟡 after the rows landed. §3.11 is `pdf/ops/`; these two
  sit at `pdf/`'s root beside the deleted `PdfModule.cpp` and need either rows or a folded row with a
  ⤵ breadcrumb.
- ~~🟡 **`platform/win/`'s four Stage 2 implementations — rows exist, tagged ○ (S2); the CODE does not.**~~
  **CLOSED AT T16.**
  Re-measured 2026-09-02: `WinDisplayTopology`, `WinPowerSession`, `WinFontResolver` and
  `WinAcceleratedSurface` each have a §3.12 row already, so what is owed is the ○ → ✅ flip at **T12**
  and **T16**, not the rows themselves. Stage 1 T7 set the precedent of adding an
  implementation's row in the same change as the implementation.
  **Half paid at T12**: `WinDisplayTopology` and `WinPowerSession` are ✅, and `WinMessageWindow` — a
  file this plan's §4 did not list — got a row of its own in the same change. **The other half at T16,
  and the two halves are not the same kind of payment**: `WinFontResolver` is ✅, and
  `WinAcceleratedSurface` was **re-tagged ○ (S4) rather than built**, because the guide's caveat table
  has always put accelerated presentation at Stage 4 *if A2 says it is needed* and nothing has yet said
  so. A row that says which stage owns it is paid; a row that says ○ (S2) after Stage 2 ends is debt.
- ~~**`tools/golden-update/` has no row**, though `tests/golden/` does.~~ **Stale — re-measured
  2026-09-02: §3.17 has one, tagged ○ (S2). PAID AT T17**, which flipped both rows ○ → ✅ and added
  five `golden/` data rows the manifest had never listed. Worth keeping as a two-line history: the
  item was raised as a missing row, was already false when raised, and was finally closed by the task
  that made it true — which is the same *a claim written by whoever had not built the thing* shape
  as **I-10** and as §6's stale walkability list, in its mildest form.
- ~~**`cmake/PdfiumAssert.cmake` has no row**, though `cmake/ToolchainAssert.cmake` does.~~ **Paid at
  T2**, in the same change as the assertion itself (D2-3).
- ~~🟡 **`Tech-Stack-Plan.md` §3.3 gained A2's PROBE verdict at T3 and **R5** is struck through; the
  PRODUCT's figure is still owed by T18 (D2-1).**~~ **PAID AT T18, 2026-09-06.** §3.3 now carries both
  figures with the distinction between them stated at the point of use — service time versus queued
  latency — and R5's row records the product's number as the one that is a claim about the product.
  The original wording of this item was that a retired risk still reading as open is how a later
  reader re-does settled work; the half-answered case turned out to be worse, because **the probe's
  31× headroom read as comfort that the product does not have.** The product's margin is 1.18×.
- ~~🟡 **`ui/viewer/`'s four rows are ○ (S2).**~~ **Paid at T13**, all four flipped in the same change
  as the code — and two rows the manifest did not list were added with their files, `app/BackendRasterizer.h`
  (issue **I-09**) and `platform/win/WinMessageWindow` at T12. `ui/shell/DocumentWindow`'s row moved from
  the D1-2 stub's description to what it now is.

- ~~🟡 **`CLAUDE.md`'s "Still no document code" paragraph**~~ **Paid — both halves: the paragraph was rewritten at T13, and the CI prefix at T18 (below). Re-read 2026-09-14; the item was left 🟡 after its own PAID line.** — *"nothing in `core/doc/`, `pdf/cos/`,
  `pdf/render/`, `pdf/ops/`, or `ui/viewer/`"* — is false after T13 and must be rewritten, not
  amended. **Rewritten twice already rather than left to rot at T18**: at T2 for `pdf/render/`, and
  at T5, when `pdf/cos/` stopped being empty and the sentence had to name what the lexer does *not*
  know as well as what it does. **Rewritten at T13, which is the rewrite it was waiting for**: `ui/viewer/` is no longer empty, so the
  sentence now says what is on screen and what is deliberately not (no zoom command, no status-bar
  number, no settings key — T14 and T15). The same file's gate table
  gained the narrowed `FPDF_` rule at T2 and its test-target table gained `rawpage_render_tests`;
  **the fourth CI job's prefixes are still owed**, and `platform.` and `render.` both grew at T12 and
  T13 without CI being told.
  **Re-measured at T16, and the gap is narrower and worse than this item said.** The summary job's
  *what did not run* list names `ui.*`, `platform.*` and `bench.*` and omits `render.*`. And the
  **windows job's non-empty-prefix loop names `unit`, `ui`, `platform`, `bench` — and not
  `render`**: so the one suite that links the engine is the one suite whose disappearance CI would
  not notice, and a build that dropped it would still report *100 % tests passed* over 40 fewer
  cases. That is precisely the failure the loop exists to catch. **One word in
  `.github/workflows/ci.yml` fixes it and it is T18's**, with the rest of this stage's CI work.
  `tests/render/CMakeLists.txt` carried a comment asserting the opposite — that the loop already
  extended to it — which was corrected at T16 in the same sweep that found this.
  **PAID AT T18, 2026-09-06**, and it was one word: `render` joins the loop, which now also covers
  T17's `render.golden.*` because that prefix lives deliberately inside `render.`. The summary job's
  *what did not run* list gained `render.*` in the same change, and both A2 figures are surfaced in
  the log beside the R-9 one — a number buried in a passing `ctest` line is a number nobody reads,
  and D2-1's point is that the two must be read together. **A fourth CI job also arrived**,
  `portable-fuzz`, which is a debt this list never named: A-25 and A-26 had been recorded as *Linux
  jobs* since T5 and T6 and **no Linux fuzz job existed**.
- ~~🟡 **`ui/shell/MessageBar` is ○ (S2).**~~ **Paid at T14**, with `ui/dialogs/PasswordDialog` added
  beside it as a row this §4 did not list — the third such file this stage, after `WinMessageWindow`
  (T12) and `app/BackendRasterizer.h` (T13). Three in three tasks is worth noticing: a manifest written
  before the code is a good manifest and is not a complete one, and the pattern each time has been the
  same — a shared mechanism two named files both need, or a seam that only becomes visible when
  something tries to test it.

- ~~🟡 **The sixteen settings keys are unwritten.**~~ **Paid at T15.** No file was added: the whole
  change is sixteen `.def` rows, two lines in `shippedPages()`, and the wiring that reads them —
  which is what convention 17 promises and the first time this project has cashed it at scale.
  `FitMode` MOVED from `ui/viewer/` into `core/render/DpiPolicy.h` in the same change, because
  `pageDisplay/defaultZoom` is a settings key and `SettingsKeys.def` may only name `core/` types.

- **`Features.md` §8's Viewer table gains no rows and must gain none.** Stage 2 handles the subset
  `Implementation-Plan.md` lists; `Ctrl+4` (Reflow), `Ctrl+R` (rotate **pages**), `S`, `Ctrl+B`,
  `Ctrl+E`, `Ctrl+L`, `Ctrl+U`, `Ctrl+'`, `Ctrl+;`, `Ctrl+Alt+C`, `Ctrl+.`/`Ctrl+,` and
  `Ctrl+]`/`Ctrl+[` belong to Stages 3–13 and stay `NotFound` — which, per Stage 1's convention,
  deliberately leaves the key unconsumed. Recorded here because a stage that implements *most* of a
  context's table invites someone to complete it. **Held at T14, and asserted rather than trusted**:
  the registration census is exact (48, not "about 48"), a case walks the 23 chords this stage owns and
  a companion case walks seven of the ones it does not, requiring each to be `NotImplemented` and to
  leave the key unconsumed.
- **`Implementation-Plan.md` § Stage 2's component list needs two corrections in the same change as
  this plan**: `PagePresentation`'s signature names `PageTree` and `RenderCache::invalidate` names
  `Scope`/`Revision`, all three of which that document assigns to Stage 3 (**D2-5**); and the
  `pdf/cos/` filter list is nine, which is right, while the FileMap's is ten (**D2-4**).

---

## 11. Explicitly out of scope

Planned absences. Filing them as bugs wastes a triage cycle and, worse, invites someone to build them
early — which is how a stage's risk retirement gets scrambled.

| Not in Stage 2 | Arrives at |
|---|---|
| **Any mutation of any kind.** No `Command`, no `CommandJournal`, no save, no `mutation-lint`. The document is strictly read-only, and `Ctrl+S` stays `NotFound` | Stage 3 |
| `pdf/cos/IncrementalWriter`, `ObjectAllocator` — the **writer** half of the COS layer (D2-4) | Stage 3 |
| `core/doc/Document`, `DocumentId`, `Outline`, `Metadata`, and the **mutating** half of `PageTree` (D2-5) | Stage 3 |
| Text selection, text extraction, search, `PdfiumTextExtractor`, `ContentStreamParser` | Stage 5 |
| Annotations, forms, security, signatures, export | Stages 6–11 |
| **Split views, rulers, grid, guides, full-screen, presentation, session restore.** `Ctrl+U`, `Ctrl+'`, `Ctrl+;` and the Full Screen context stay unregistered | Stage 4 |
| The sidebar's document panes — thumbnails, bookmarks, destinations | Stage 4 |
| Reflow view (`Ctrl+4`) — it needs the tagged-structure model | Stage 13 |
| `pdf/cos/StandardSecurityHandler`, `CryptFilter`, and `core/doc/Permissions`. An encrypted file is opened for reading through PDFium (**D2-6**) and the COS reader declines it by name | Stage 10 |
| **`WinAcceleratedSurface` doing anything.** The interface is declared; the tile presenter paints through Qt this stage. Accelerated presentation is an optimization, and optimizing before A2 is measured is how a measurement gets contaminated | Stage 4, if A2 says it is needed |
| The remaining 21 Preferences groups — five ship after T15 of 26 | Stage 14 |
| `general/touchMode` doing anything — still declared, still read by nothing (Stage 1's I-38) | Stage 14 |
| Dr. Memory / Application Verifier **in CI** — it is a manual case this stage, for want of the tools on the runner (§9) | Stage 15 |

---

## 12. Risks inside Stage 2

| Risk | Shape | Mitigation |
|---|---|---|
| **The corpus is entirely our own output, so the golden suite is partly circular** (D2-11) | A generator emits only what its author already understood. `tests/golden/` therefore proves we rasterize **our own files consistently**; it does not prove we rasterize **real-world PDFs correctly**. Real-producer constructs — a scanner's CCITT quirks, InDesign transparency, an unusual colour space, a broken-but-tolerated producer string — are outside Stage 2's evidence, and a green golden suite implies a stronger claim than the one we can make | **Named rather than mitigated away, which is the honest treatment.** Three things bound it: T4's generators are held to a deliberately awkward bar (D2-11) so the corpus is wider than a naive one; `XrefRepair` and the fuzz targets attack malformed input continuously, which is where real-world files actually differ most; and **§13.6 states the limit as a Definition-of-Done item**, so it is signed off knowingly rather than discovered later. The trigger for revisiting is the first real-world rendering bug the corpus cannot reproduce |
| **A2 comes back no-go** | D10's tiling model is wrong and T10/T11 are rebuilt around something else — a whole-page rasterization with progressive refinement, most likely | **T3 is third**, before any tiling code. The probe is throwaway by design so a no-go costs three days rather than three weeks |
| **The first-party COS reader is a bigger job than the stage admits** | T5–T8 are four tasks covering a lexer, a parser, an xref engine with incremental chains, nine filters and a repair scanner — arguably a stage of their own | It is scoped to a **reader** (D2-4), which is roughly half; the fuzz targets arrive with the code rather than after it; and `Implementation-Plan.md`'s exit criteria only require that malformed files yield a report and never a crash, not that we out-parse PDFium. **If it overruns, the honest response is to say so in the status block, not to quietly narrow the corpus** |
| **PDFium's rasterizer output shifts under us** | Golden baselines silently rebase and a real regression ships | **D2-3**: the version is pinned and asserted at configure, and `golden-update` demands a reason |
| **A green render suite over a viewer nobody has looked at** | Stage 1's I-36 exactly — eleven cases asking what a widget *contained* while the layout gave it zero pixels | §1's rule: assert the geometry the layout gave, then the consequence. `QWidget::grab()` works offscreen, so a case can assert **painted pixels** rather than a queue's contents |
| **Cancellation is tested through the scheduler's own API** | Convention 19's back door — the queue empties correctly and the gesture that should empty it is never driven | Cases assert the worker **never started** the request, and the scroll-driven case says at the assertion what it does not cover |
| **`core/doc/` grows past D2-5's four types** | Stage 3's model arrives early, unplanned, and half-built | D2-5 names exactly four, and `PageTree` ships read-only. A fifth type appearing in `core/doc/` during Stage 2 is a finding |
| **The `ui/` warning grace list gains its first entry** | D1-12's *"may only shrink"* becomes decorative on the largest `ui/` addition since T10 | An entry needs a named reason and an owning issue number. `ui/viewer/` is new code with no legacy, so there is no honest reason for one |

---

## 13. Definition of Done for Stage 2

[`Implementation-Plan.md` §2's Definition of Done](./Implementation-Plan.md#definition-of-done--every-stage-no-exceptions),
made concrete:

1. T1…T18 complete; the [exit criteria](./Implementation-Plan.md#stage-2--pdf-engine-boundary-and-tiled-rendering) met.
2. `cmake --build --preset x64-release --target checks` clean — still **three** gates, with the
   narrowed `FPDF_` rule and its control.
3. `ctest` green on Windows across all five targets, including every Stage 0 and Stage 1 suite.
4. `portable-asan` green over the **whole corpus parse**, and `portable-fuzz` green for 10 minutes on
   both targets — the first time either has had a target to run.
5. **Golden-image suite green and its baselines committed.** Round-trip (`tests/roundtrip/`) is **not
   applicable** — it arrives at Stage 3 with the writer. Stated so its absence is a recorded fact.
6. **The corpus limitation is stated in the sign-off, not merely known.** The corpus is entirely
   generated (D2-11), so the golden suite's green is a claim about **consistency against our own
   output**, not about **fidelity to real-world PDFs**. Stage 2 is signed off with that written into
   `Implementation-Plan.md`'s Stage 2 status block in those words — an unqualified *"every corpus file
   renders correctly"* would overstate the evidence, and the overstatement is the kind that survives
   for stages because nobody re-reads an exit criterion that already passed.
7. **Dr. Memory / Application Verifier clean over the raster path** — the manual substitute for the
   ASan coverage PDFium's presence forbids (§9, risk R7).
8. [`Stage2-Rendering-Testing-Guide.md`](./Stage2-Rendering-Testing-Guide.md) — written from this plan
   before the code, its automated half run green by me with **actual** output, its manual half walked
   and signed off **by you**. I do not mark a manual case.
9. No open S1 or S2 in the guide's issue log.
10. **A2 has a recorded verdict with numbers** — both the probe's and the product's (D2-1) — and
   **O7 and O10 are resolved** in `Implementation-Plan.md` §8.
11. `Architecture-FileMap.md` rows flipped ○ → ✅ and §10's missing rows added, in the same change.
12. `Implementation-Plan.md` Stage 2 status block updated to reality, deviations included; risk **R5**
    struck through in `Tech-Stack-Plan.md` §10.
