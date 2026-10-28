# RawPage — Deferred Work Register

**Work that a stage deliberately did not do, and that no later stage's plan has yet taken in.** Created
2026-09-27, when `Stage4-ViewingSurface-Testing-Guide.md` §8's **I-07** was found to name Stage 8 as its
owner while Stage 8's plan in [`Implementation-Plan.md`](./Implementation-Plan.md) §4 said nothing about
it. A deferral that only the deferring stage records is a sentence nobody downstream reads
(`CLAUDE.md` → *How to write it*, rule 4) — this file is where such an item waits, visibly, until a stage
takes it.

## What this file owns, and what it does not

| Owns | Does not own |
|---|---|
| Each deferred item's **state of ownership**: *unassigned*, or *taken by Stage N* with where | **What the capability is** — `Features.md`, which keeps the line in scope |
| **What exists today** and **what is absent**, as facts checked on a named date | **The defect or finding itself** — the raising stage's guide §8 (`I-nn`) keeps the mechanism and the history |
| Why it was deferred, in one paragraph | **How it will be built** — a repair written by someone who did not build it is a guess (rule 4); the stage that takes it decides |
| | **Anything removed from scope** — `Features.md` → *Removed from scope* is that record |

## The rules

1. **An entry is added when a stage defers work and no later stage's plan names it.** A deferral that
   a later stage's plan *already* lists does not belong here — that plan is its owner.
2. **An entry leaves *unassigned* only when a stage's plan takes it in** — its §4 block in
   `Implementation-Plan.md` or its `Stage<N>-*-Plan.md` — and the entry then records **which stage and
   where**. When that stage builds it, the entry records the commit or the task, and is marked done.
3. **Rows are never deleted.** A finished or withdrawn entry keeps its row with its outcome, as a
   `⤵` breadcrumb does in `Architecture-FileMap.md`.
4. **Labels are `DW-n`**, numbered in order and never reused. Cite one as *`DW-1` (`Deferred-Work.md`)*
   on first use, per `CLAUDE.md` → *Citing a label*.
5. **Re-check the *What exists* column before relying on it** — it is dated because code moves.

## Register

| # | Item | Raised by | State | Owner |
|---|---|---|---|---|
| **DW-1** | **Substitute page colours** — the second half of `Features.md` §1.2's *"invert / substitute page colors for high-contrast reading"* | `Stage4-ViewingSurface-Testing-Guide.md` §8 **I-07** (S4), Stage 4 T8, 2026-09-21 | ⚪ **Deferred** by the user, 2026-09-27 | **Unassigned** — no stage's plan names it |
| **DW-2** | **A vector snapshot** — `Features.md` §8's `S` (*"copy a dragged region as an image"*) as a vector metafile that stays sharp when scaled, rather than a bitmap | `Stage5-TextSearch-Plan.md` §0 **Q3** and **D5-10**, written 2026-09-29 before any Stage 5 code | ⚪ **Deferred** by the user, 2026-09-29 (option B of three) | **Unassigned** — no stage's plan names it |
| **DW-3** | **Typed comments in scripts outside WinAnsi and the four CJK collections** — `Features.md` §4.1's text box, callout, typewriter and dynamic stamp showing Cyrillic, Greek, Hebrew, Arabic, Thai, Devanagari and every other script a font must be embedded for — **and, since 2026-10-29, the text Stage 7 draws on pages**: headers, footers, watermarks, Bates prefixes and suffixes, the table-of-contents page (`Stage7-Assembly-Plan.md` §0 **Q5(e)**) | `Stage6-Annotations-Plan.md` §0 **Q3** and **D6-7**, written 2026-10-04 before any Stage 6 code | ⚪ **Deferred** by the user, 2026-10-04 (Q3 approved as proposed) | **Unassigned** — no stage's plan names it |
| **DW-4** | **An annotation burst inside A2's frame budget** -- `Stage6-Annotations-Testing-Guide.md` §3.1's scale for *A2 at Stage 6*, which neither T4's bench nor T15's product reading met | `Stage6-Annotations-Plan.md` **D6-24**, Stage 6 T4 (2026-10-05) and T15 (guide **A-104**), 2026-10-20 | ⚪ **Deferred** by the user, 2026-10-20 (the first of three options); **split 2026-10-29** | **Cause (2), the show step: taken by Stage 7** -- `Stage7-Assembly-Plan.md` §0 **Q4**, task **T4** (approved by the user 2026-10-29). **Cause (1), the annotated page's tile: unassigned** -- no stage's plan names it |

---

### DW-1 · Substitute page colours

**The capability.** `Features.md` §1.2, *View transforms*: *"Invert / substitute page colors for
high-contrast reading"* and *"Set a custom page background color"*. The **invert** half is built; the
**substitute** half — a chosen page colour and text colour in place of the document's — is not.

**Why it was deferred.** Substitution needs a colour pair that persists, and at Stage 4 T8 no declared
settings key held one; minting one then would have been a key no plan asked for, chosen by the task
that happened to notice (I-07's write-up). I-07 named **Stage 8's colour work** as the owner. **Checked
2026-09-27, that was not true**: Stage 8's plan (*Save, export, and optimize*) carries export colour
space only, `Implementation-Plan.md` §7 maps `Features.md` §1.2 to Stages 2 and 4 alone, and Stage 14's
*"complete Preferences set"* surfaces declared keys rather than declaring this one. Offered three ways
to settle it — build it inside Stage 4, assign it to a later stage's plan, or remove it from scope —
**the user chose to defer it** and record it here.

**What exists today** (checked 2026-09-27, commit `6c4a9b6`):

- `core/cmd/ActionId.def` declares **`view.pageBackground`** (`ViewPageBackground`, context `Viewer`,
  catalog key `action.view.pageBackground`); **it is registered by nothing**, and
  `ui/shell/MainWindow.cpp` says why at the `view.invertColors` registration.
- **Two cases assert that absence**: `tests/ui/tst_rulers.cpp` (guide case **A-52**, recorded 🟡 for
  this half) and `tests/ui/tst_shell.cpp`. Either fails the day the id is registered, which is
  deliberate — whoever takes this item updates them in the same change.
- **`view.invertColors`** is registered: a per-VIEW toggle with no chord and no settings key, painted
  in `ui/viewer/PageView::paintContent` over the tiles the scheduler returned, one page sheet at a time,
  with the grid and guides drawn over it — a paint, not a render flag (D4-11,
  `Stage4-ViewingSurface-Plan.md` §3). Toggling it re-renders nothing.
- **Precedent for a stored colour**: `core/settings/SettingsKeys.def` holds `units/gridColour` as a
  `QString` (`"#6699CC"`), parsed in `ui/viewer/GridGuideLayer`, because `core/` may not name
  `QColor` (convention 3).

**What is absent:** a settings key (or pair) holding a page colour and a text colour; any Preferences
control for them; a registration for `view.pageBackground`; and a paint of substituted colours. No
design decision exists for whether substitution is per view like the inversion or persistent like
the grid colour, or how it combines with the inversion — the stage that takes this decides.

**Taken by:** —

---

### DW-2 · A vector snapshot

**The capability.** The Snapshot tool (`S`, `Features.md` §8's Viewer table) copies a dragged region to the
clipboard. As a **bitmap** it is Stage 5's; as a **vector metafile** — which Word and PowerPoint keep sharp at
any size and print from — it is this entry. `Architecture-FileMap.md`'s `WinRichClipboard` row once promised
it among *"bitmap, vector-metafile, and HTML clipboard flavours"*.

**Why it was deferred.** Qt already publishes plain text, HTML (as CF_HTML) and bitmaps to the Windows
clipboard, which is everything Stage 5's copy and snapshot need. What Qt cannot do is turn **PDF content**
into vector drawing commands, and the one engine call that can — PDFium's `FPDF_RenderPage(HDC, …)` — takes a
Windows device context: `pdf/render/` may not be OS code, only buffers and callbacks may cross
`core/backend.h`, and PDFium may be called only from the render thread. Offered three ways on 2026-09-29 — a
bitmap at the view's scale (A), that plus a resolution setting (B), or build the vector path now (C) — **the
user chose B**, which removes most of the practical cost, a blurry print, with one settings key
(`pageDisplay/snapshotDpi`, `Stage5-TextSearch-Plan.md` D5-11), and leaves the vector flavour here.

**What exists today** (checked 2026-09-29, before any Stage 5 code; **re-checked 2026-10-01 after Stage 5
T5** — every line below still holds, and text copy now goes through `QClipboard` with its HTML measured
reaching Windows as CF_HTML, `Stage5-TextSearch-Testing-Guide.md` A-50):

- `platform/platform.h` declares **`IRichClipboard`** — `setVectorMetafile`, `setHtml`, `vectorMetafile()`,
  `html()` — and **nothing implements it**; the platform census is **6 of 18**.
- The pinned PDFium DLL **exports `FPDF_RenderPage`** (`third_party/pdfium/pdfium.def`), declared under
  `_WIN32` in `third_party/pdfium/include/fpdfview.h`, and nothing calls it.
- `Implementation-Plan.md` § Stage 12 lists `platform/win/WinRichClipboard` for its **paste** path —
  *reading* the clipboard to create a PDF. That is not this item, and does not make Stage 12 its owner.
- Stage 5's snapshot is **planned** as a bitmap at `pageDisplay/snapshotDpi`; it is not built yet.

**What is absent:** any producer of vector page content for the clipboard; a sanctioned place for an
engine call that takes a Windows device context; and any measurement of what PDFium's `HDC` path emits for
transparency, soft masks and shadings — a print path may turn those into bitmaps inside the metafile, which
would make the *vector* snapshot partly a bitmap anyway. The stage that takes this decides.

**Taken by:** —

---

### DW-3 · Typed comments outside WinAnsi and CJK

**The capability.** `Features.md` §4.1's typed annotations — *Text callout*, *Free text box*,
*Typewriter-style free text* and the *dynamic stamp* that fills in the author — showing whatever a person
types. Stage 6 shows **Latin in WinAnsi** (base-14 Helvetica) and **Chinese, Japanese and Korean** (the four
predefined Adobe CID collections, not embedded). Every other script is this entry. **Widened 2026-10-29** by the
user's answer to `Stage7-Assembly-Plan.md` Q5(e): the text Stage 7 DRAWS into page content — headers, footers,
a watermark's text, Bates prefixes and suffixes, a table-of-contents page — follows the same fonts and the same
refusal by name, for the same reason. Text Stage 7 only STORES (bookmark titles, layer and destination names)
is not drawn and is not this entry.

**Why it was deferred.** A typed annotation's appearance stream needs a font that every reader can draw.
Base-14 fonts cover WinAnsi alone, and the predefined CID fonts cover the four CJK collections because
readers carry or substitute faces for them. Anything else needs a font **embedded** in the document, and
embedding one sensibly means **subsetting** it — a whole system face is megabytes per document, tens for
CJK. Subsetting is not built: `pdf/ops/OptimizeOps` (Stage 8) names font subsetting for optimization, and
HarfBuzz arrives with Stage 13's `shaping` feature. Offered on 2026-10-04 with the alternative of
embedding whole fonts, **the user approved the proposal**: refuse such characters at input, by name, and
record the gap here (`Stage6-Annotations-Plan.md` D6-7).

**What exists today** (checked 2026-10-04, before any Stage 6 code; **re-checked 2026-10-07 at Stage 6 T7**):

- **Since Stage 6 T7, typed annotations exist and the refusal is built** (`Stage6-Annotations-Plan.md`
  D6-34): `core/doc/TypedText`'s `firstRefusedCharacter` names the first character no face can show, the
  editor refuses it at input -- a key, an input method's commit, a paste, a drop -- and the builder refuses
  an appearance holding one (`error.edit.typedCharacterRefused`). Which CJK characters are shown is a
  generated table (`core/doc/CjkCoverage.inc`) restricted to CJK scripts -- GB 2312's Greek is refused all
  the same -- and held to PDFium character by character (`tests/render/tst_typedtext.cpp`). The editor's
  refusal is what a person meets of this entry.
- Stage 5 T1 measured PDFium drawing a non-embedded `STSong-Light` through `UniGB-UCS2-H` with this
  machine's face (`STSong -> SimSun`, `corpus/ordinary/cjk-readable.pdf`) — the path D6-7 relies on for
  CJK.
- Stage 6's M-06 measured that path's bound in Acrobat, 2026-10-28 (`Stage6-Annotations-Testing-Guide.md`
  I-47): Acrobat DC without Adobe's optional font pack lacks the predefined CJK CMaps and draws none of
  RawPage's CJK typed comments; Firefox and SumatraPDF draw them. The user accepted it rather than embed —
  so the non-embedded CJK faces, too, are what an embedded subset would replace.
- `vcpkg.json`'s `shaping` feature (HarfBuzz and ICU) is **off** (`Stage5-TextSearch-Plan.md` D5-5).
- No first-party code reads, subsets or writes a font program -- T7 writes font DICTIONARIES naming the
  base-14 Helvetica and the four CJK collections, and no program.

**What is absent:** a font subsetter; a policy for choosing a system face per script and checking its
embedding permission (`OS/2.fsType`); `/ToUnicode` generation for an embedded subset; and any measurement
of how Acrobat and Firefox draw a subset RawPage embeds. The stage that takes this decides.

**Taken by:** —

---

### DW-4 · An annotation burst inside A2's frame budget

**The capability.** `Stage6-Annotations-Testing-Guide.md` §3.1's scale for *A2 at Stage 6*: while annotation edits
arrive -- one every 100 ms for 30 s on page 450 of `pages-900.pdf` -- the scrolled viewport's p95 queued latency
inside **16.667 ms**, and an edit's time to the pixels **≤ 100 ms** at p95 and **≤ 250 ms** at most. Not a
`Features.md` line: a performance bound on every annotation tool (`Tech-Stack-Plan.md` §3.3, *A2 at Stage 6*).

**Why it was deferred.** *A2 at Stage 6* was measured twice, by `Stage6-Annotations-Plan.md` D6-24. At T4
(`bench_pipeline --stage6`) the user chose **`go, with named custom work`** and the work was built in the task -- an
annotation edit drops only the tiles its rectangle reaches, and a revision re-keys every tile it does not reach
(guide **I-06**) -- and the figures were still outside §3.1. At T15 the figure was re-read through the shipped
surfaces (`bench_surface --stage6`, guide **A-104**: the real pencil drawing in one pane of a split window while the
other scrolls) and through T4's bench with §3.1's real edit, an ink stroke of 200 samples. **Both are still outside
§3.1.** Offered on 2026-10-20 a choice of recording it, building a compose that stops growing with the unsaved
annotations inside T15, or re-grading the verdict as `no-go` (which changes D6-2 to a mirror that T4 measured would
share both costs), **the user chose to keep the verdict, record the figures, and defer the rest here.**

**What exists today** (measured 2026-10-20, Stage 6 T15):

- **The product's figure** (`rawpage_bench_surface --stage6`, nine 30-second runs in three sets, offscreen, 288 dpi
  over a 1920x1080 viewport divided between the two panes): the scrolling pane's Visible p95 **21.9-29.4 ms**
  against **1.38-1.61 ms** with nothing drawn; edit-to-pixels p95 **201-505 ms**, max **0.87-4.2 s**; the release --
  the stroke made and its command run on the UI thread -- p95 **88-143 ms**; 300 strokes and 300 commands a run. The
  first set read faster than the other two (release p95 88-90 against 135-143 ms) and the machine, not the code, moved:
  the bench's viewer-alone column, which runs no annotation code, read 20-28 % slower in the later session too.
- **The bench's figure** (`rawpage_bench_pipeline --stage6`, four runs over two sessions, the edit an ink stroke of
  200 samples): viewport p95 **107-273 ms** on the three files against **9.2-16.8 ms** with no edits; edit-to-pixels
  p95 **307-414 ms**, max **353-526 ms**; `journal.run` p95 **90-133 ms** (compose and verify, the verify alone p95
  41-43 ms on the 900-page files in the first session); the reload p95 **2.2-3.0 ms**; a tile of the edited page p95
  **50-70 ms**, 11.8 s of the engine's 30; the path geometry of a page p95 **0.11-0.30 ms** (these pages hold no
  paths).
- **Two causes, measured, neither one the reload:** (1) **a tile of a page carrying hundreds of annotations
  costs ~50 ms by itself** -- PDFium draws every annotation of the page into every tile it renders -- so the
  scrolled pane's tiles queue behind the edited page's; (2) **the journal's show step composes and verifies the
  whole update on the UI thread on every edit**, re-serializing every unsaved annotation and re-reading the
  file, so `journal.run` grows with the unsaved annotations (p95 88-143 ms by 300 strokes, over both sessions) and the pump waits
  behind it.
- **Manual case M-12** (`Stage6-Annotations-Testing-Guide.md` §6) is a person's judgement of twenty strokes and
  a scroll on the same file; not walked on this date -- walked 2026-10-28 and passed (64 strokes on page 450 and
  three highlights judged immediate and smooth); A-104's figures above are unchanged by it.

**What is absent:** a show step whose cost does not grow with the unsaved annotations; a re-render of an
annotated page whose per-tile cost does not grow with the page's annotations; and a figure for a person's rate
of drawing rather than §3.1's burst. The stage that takes this decides how.

**Taken by:** **cause (2), the show step that grows with the unsaved update -- Stage 7, task T4**
(`Stage7-Assembly-Plan.md` §0 **Q4**, approved by the user 2026-10-29; graded by
`Stage7-Assembly-Testing-Guide.md` §3.7). Stage 7 took it because its commands make the unsaved update large in
one gesture -- an insert of copied pages, a header on every page -- where Stage 6 needed hundreds of strokes.
**Cause (1), a tile of a page carrying hundreds of annotations: unassigned.** The entry stays open until both
are done.
