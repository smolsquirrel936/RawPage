# Stage 6 — Annotations and Review · Testing Guide

> **Authoritative verification document for Stage 6.** Written from
> [`Stage6-Annotations-Plan.md`](./Stage6-Annotations-Plan.md) and
> [`Implementation-Plan.md` § Stage 6](./Implementation-Plan.md#stage-6--annotations-and-review),
> **before any Stage 6 code exists** — which is the point: verification written afterwards only ever
> confirms whatever got built, including the parts built wrong.
>
> **Status:** ⚪ **Written 2026-10-04 from the plan's draft, the day Stage 5 was signed off, and before
> any Stage 6 code. The plan's §0 was ANSWERED the same day, all eight — Q1 … Q6 and Q8 approved as
> proposed, and Q7 by moving the custom stamp from a PDF page to Stage 7, which rewrote A-78 and M-07.
> No case had a result until T1.** **T1 (2026-10-05): A-13 … A-16 ✅**, each with its evidence. **T2
> (2026-10-05): A-17 … A-22 ✅ and A-23's Windows half** — its `portable-fuzz` log read from CI run
> 37250978598, its soak log still owed. That run's `portable-asan` job failed A-22's clock (**I-05**). **T3
(2026-10-05): A-24, A-25, A-28, A-29, A-30 and A-33 ✅; A-26 (the rule, at `AnnotationOps`), A-27 and
A-32 (Highlight, the one type T3 writes) in part, each saying what is owed; A-31's CI step written, its log
owed** -- thirteen planted controls, all fired. **T4 (2026-10-05): A-35, A-37 and A-38 ✅ -- A-38 a
VERDICT, *A2 at Stage 6* `go, with named custom work` (the user's choice), its named work built in the task
and what it did not bring inside recorded; A-34 (Highlight, through commands), A-36 (all but its search
clause, T9's) and A-39 (all but the status bar, T5's) in part** -- sixteen planted controls, all fired, one only after its case was strengthened (I-06's entry and the T4 row say which). **T5
> (2026-10-05): A-39, A-40, A-41, A-42, A-44, A-45 and A-48 ✅; A-28 and A-29 re-run ✅ for the seven commands; A-26, A-34,
> A-35, A-43, A-46 and A-47 in part, each saying what is owed** -- fourteen planted controls, all fired, two only after the plant
> itself was repaired (the T5 row says which). **T6 (2026-10-05): A-49 … A-54 ✅; A-28 and A-29 re-run ✅ with
> T6's shapes; A-27, A-32, A-34 and A-35 extended to every kind T6 makes, each through its TOOL** -- planted controls listed
> in the T6 row. **T7 (2026-10-07): A-55, A-56 and A-57 ✅; A-28 re-run ✅ with T7's shapes; A-26, A-27, A-29, A-32,
> A-34 and A-35 extended to free text, A-34/A-35 through the typed TOOLS** -- fifteen planted controls, all fired, one
> only after its case was strengthened (the T7 row says which). **T8 (2026-10-08): A-46, A-47's key, A-58 … A-66 ✅
> (A-64 a new suite of 31 appearance baselines, each LOOKED AT before it was kept; A-65 PDFium's own generation as the
> control); A-28 re-run ✅ with T8's shapes; A-26, A-27, A-29, A-32, A-34 and A-35 extended to every drawing kind, A-34/A-35
> through the drawing TOOLS** -- planted controls in the T8 row of `Implementation-Plan.md`. **T9 (2026-10-08): A-67 … A-73 ✅, A-36's search
> clause and A-43's *listed hidden* ✅, A-74 all but its attachment clause (T11's); A-28 re-run with T9's two commands** -- eighteen planted
> controls, all fired, one only after its case was strengthened (the T9 row). §8 logs **I-01 … I-22**, all
> closed (I-11, S3, T7's push not building under ASan, by CI on 2026-10-08): I-01 … I-06 S3, **I-07 (S2) and I-08 (S1)
> found by T5's own cases before any commit, I-09 (S2) by T6's A-50 before any commit, I-10 (S2) by T7's A-56
> engine case before any commit, I-12 (S2) by T8's A-65 before any commit, I-13 (S1), I-14 (S3) and I-15 (S3) by T9's own cases before
> any commit, I-16 (S3) by T11's run of an earlier case before any commit, I-17 (S2) by the nightly soak over T11, I-18 (S2) by reading I-17's code, I-22 (S2) by the first soak over T14 (run 39226515635)**. **T10 (2026-10-09): A-75 … A-80 ✅, A-81's image clause (its annotation clauses T13's); A-26, A-27, A-32, A-34
> and A-35 extended to stamps, A-34/A-35 through the stamp TOOL; A-28 and A-29 re-run with stamps; A-40's stamp clause** --
> eighteen planted controls (the T10 row of `Implementation-Plan.md`). **T11 (2026-10-12): A-82 … A-86 ✅, and A-74's attachment clause
> -- so A-74 ✅ whole; A-26 extended to the file's own pushpin; A-28 and A-29 re-run with the two attachment commands** -- eighteen planted
> controls, all fired, one only after its case was strengthened and two only after the plant itself was repaired (the T11 row of
> `Implementation-Plan.md`); **I-16 (S3)** found by an earlier case before any commit, and closed. **T12 (2026-10-14): A-87 … A-92 ✅
> (A-90 against the corpus drawing's own lengths, a deviation its result says); A-26 extended -- T8's refusal of a line with leaders
> REWRITTEN, such a line now redrawn -- and A-27, A-32 and A-34 to measurements, A-34 through the measuring tools** -- planted controls in the
> T12 row of `Implementation-Plan.md`; **I-19 (S2)**, a curve snapped to a crossing that is not there, found by the task's own engine case before any commit, and closed. **I-17 (S2)**, a pushpin
> read holding a nameless file, found by the nightly soak over T11 (run 38384043765) and closed 2026-10-14; **I-18 (S2)**, a file read with
> a name or key past the bound a CREATED one keeps that no command could move, edit or delete, found reading I-17's code and closed the same day; **I-19 (S2)** by T12's own case before any commit. **T13 (2026-10-16): A-81 ✅ whole (its annotation clauses, the cut and the bytes alone as a second process would have them), A-93 … A-99 ✅; A-23 gains its exchange branch, replayed headless; A-28 and A-29 re-run with `ImportAnnotationsCommand`** -- planted controls in the T13 row of `Implementation-Plan.md`; **I-20 (S3)**, a pop-up given a name a comment of the file already had, found writing the fuzz branch's NAMED invariant before any run, and closed. **T14 (2026-10-16): A-07 and A-100 ✅; A-101 ✅ but its by-hand recording run on the `windows` platform, which is owed; A-81, A-93 and A-94 extended to the mark and the sound; A-06 for `SoundData` owed to CI's `portable-asan` log** -- seventeen planted controls, all fired (the T14 row of `Implementation-Plan.md`); **I-21 (S3)**, a pushpin or a sound copied from a file pasted with no bytes and refused, found reading the copy path while writing D6-41(k)'s claim, before any commit, and closed. **T15 (2026-10-20): A-01 … A-05, A-08 … A-12 and A-102 … A-108 ✅, and A-26, A-27, A-32 and A-34 whole -- every automated case now carries a result, with two halves owed: A-101's by-hand recording run and A-04's log over T15's two new cases; A-104 a VERDICT re-read, still outside §3.1, kept `go, with named custom work` by the user's choice and the rest deferred as `DW-4` (`Deferred-Work.md`)** -- nine planted controls, all fired, one (P7) only after the bench's self-check was strengthened and one (P8) replaced by P8b because it fired for the wrong reason (the T15 row of `Implementation-Plan.md`); no issue raised. **All thirteen manual cases are walkable now, and none has been walked.** **Preparing M-01 (2026-10-20): I-23 (S3, environment)** -- four `Ctrl+Alt` arming chords held by other programs' system-wide hotkeys; `Ctrl+Alt+M` (the NVIDIA overlay) released by the user and closed, `L`, `A` and `K` open. **M-08 is walkable since T14; M-04 and M-10 since T13**; M-09 since T12; M-13 since T11; M-07 since
> T10; M-05 since T9; M-06 since T7; M-02 and M-12 since T8.** **108
> automated** (A-01 … A-108) and **13 manual** (M-01 … M-13). **The cases written against Q1 … Q5 were
> re-read against the answers and stand as written** — A-40, A-46 and A-70 (Q1, A-40's Expect gaining
> the collision census at **thirteen**, the consequence of Q1 the user accepted); A-08 and A-55 (Q2);
> A-56 and M-06's step 3 (Q3); A-07, A-101 and M-08 (Q5); and A-31 (Q8). Q4 and Q6 move capabilities out of the stage, so no case is written for them — §1's
> out-of-scope table says so.
>
> **Two halves, per [`CLAUDE.md`](../CLAUDE.md): [§5 Automated](#5-automated-verification--i-run-this)**
> — I run it and report the actual output — and **[§6 Manual](#6-manual-verification--you-run-this)** —
> you walk it on real hardware, and **I never mark a manual case passed on your behalf.**
>
> **What the manual half is for at this stage.** Annotations are written for **other people's
> readers**, and the one question no assertion here can answer is whether Acrobat and Firefox show
> what RawPage shows (plan D6-6). Chrome and Edge are PDFium, so they cannot answer it. Everything
> else — geometry, identity, threads, merge, the bytes written, what survives undo and recovery — is
> arithmetic a test can reach, because the corpus generator records every annotation it wrote (plan
> §5.2). **Anything in §6 that could have been automated is a defect in this guide — say so and I
> will move it.**
>
> **Label scoping.** `A-nn`, `M-nn` and `I-nn` here are **this guide's**; Stages 0 to 5 have their
> own, and this guide cites theirs with the stage named — *"Stage 5's I-04"*. `A2` and `A4` (no
> hyphen) are **risks** in `Tech-Stack-Plan.md` §3.3. `T1`…`T15` are **Stage 6** tasks, `D6-n` Stage 6
> decisions and `Q1`…`Q8` the plan's open questions, all in `Stage6-Annotations-Plan.md` (§6, §3 and
> §0).

---

## 1. Scope

### In scope — must pass for sign-off

- **Every `Features.md` §4.1 tool in this stage creates a correct annotation with a correct
  appearance** — correct by the generator's record, by the rendered ink, and by Acrobat and Firefox.
- **What the screen shows is what Save writes** (plan D6-2): a live view and the saved file reopened
  rasterize identically.
- **An edit changes what it means to and nothing else**: unmodelled keys, content streams and signed
  ranges are untouched (D6-5, D6-25, A4).
- **Every change is a command, one gesture one undo step**, surviving save, undo, redo and a crash
  (D6-1, D6-4, D6-23).
- **The Comments pane** groups, sorts, filters, searches, threads, replies, sets status and
  checkmarks, by keyboard as well as mouse.
- **Stamps, attachments, measurement, redaction marks, FDF/XFDF exchange and migration** as the plan
  specifies them.
- **The A2-at-Stage-6 verdict and A4's product half are recorded.**
- **No regression** in any Stage 0 to 5 case, on **both** presets.

### Out of scope — do NOT file these as bugs

Planned absences, itemized in [`Stage6-Annotations-Plan.md` §11](./Stage6-Annotations-Plan.md#11-explicitly-out-of-scope).

| Not a bug this stage | Arrives at |
|---|---|
| **An encrypted document's comments are neither listed nor editable** — the first-party reader declines it (D6-3) | Stage 10 |
| **A redaction mark hides nothing** — it is a mark; applying is Stage 10's (D6-25) | Stage 10 |
| **There is no comment summary and no print-with-comments** (Q4, approved 2026-10-04; D6-19) | Stage 8 |
| **A typed comment refuses Cyrillic, Greek, Hebrew, Arabic, Thai…** (Q3, approved 2026-10-04; D6-7) | `DW-3` |
| **Comment text is not spell-checked** (Q6, approved 2026-10-04) | Stage 13 |
| **The link tool does nothing** | Stage 7 |
| **Form fields cannot be edited** | Stage 9 |
| **A signature image is not a digital signature** — it is a stamp (D6-16) | Stage 11 |
| **A custom stamp cannot be made from a PDF page** — images only (Q7, answered: D6-16) | Stage 7 |
| **No shared review, no review tracking** | Stage 16 |
| **`Ctrl+B` does nothing** and leaves the key alone | Stage 7 |
| **A page colour can be inverted and not substituted** — Stage 4's **I-07** | `DW-1` |

### What this evidence cannot tell us

- **Every automated claim is about our own generated corpus** (D6-26). Real annotations from real
  producers are met only in M-04, through what Acrobat exports.
- **"Looks the same in another reader" is judged by a person, in two readers** — Acrobat and Firefox
  (pdf.js). Not in every reader, and not on a phone unless you walk one (P-2).
- **`ui/` is outside every sanitizer this project has** (O10). `portable-asan` and
  `fuzz_annot_pipeline` cover the model, the reader, the ops, the builder and the codecs, and nothing
  about the overlay, the tools or the panes.
- **PDFium is not a sanitized dependency.** A reload that hands PDFium a freed buffer would not be
  seen by ASan, which never runs PDFium; A-37 asserts the ownership instead.
- **Latency is measured; responsiveness is judged.** A-38 measures the edit-to-pixels latency and the
  viewport's p95; whether drawing ink *feels* immediate is M-12's.

### Case anatomy

Every case is **ID · Goal · Steps · Expect · If it fails**, with a `☐` changed to ✅ or ❌. A case
either passes or yields a **specific** failure — "looks fine" is not a result.

### The rules this guide is written under

**Rule 1 — assert the EFFECT, not the intent** (Stage 2's I-28, convention 19). A command in the
journal is not an annotation in the file; an annotation in the file is not one on the screen; a
`/AP` stream is not an appearance. Where a case can only reach the intent, it says so at the
assertion and names what it therefore does not cover.

**Rule 2 — a repaired case is re-walked from step 1** (Stage 1's I-45).

**Rule 3 — a green tick is not a clean run** (Stage 2's I-38). Linux-only halves — `portable-asan`,
`portable-fuzz`, `qpdf` — are read from the CI job's **log**, never from its tick.

**Rule 4 — a widget case drives layout itself, and asserts a non-zero visible count first**
(convention 12).

**Rule 5 — a new mechanism gets a control written from what must NOT happen** (convention 22). An
undo before a save must write nothing; a mark must remove nothing; a locked annotation must not move;
a text field must keep its own `Ctrl+V` and `Esc`.

**Rule 6 — the oracle for GEOMETRY is the rendered ink** (Stage 4's I-10). Quadpoints, hit-tests,
snaps and placements are compared against the **raster**, located by code that shares nothing with
the transform that produced them.

**Rule 7 — the oracle for CONTENT is the generator**, never PDFium. The reader is graded against
`corpus/labels/annots-*.json`; a case comparing `AnnotationReader` with `FPDFAnnot_*` getters proves
the two readers agree, which is A-18's narrower job.

**Rule 8 — the oracle for APPEARANCE is not our builder** (D6-6): golden rasters LOOKED AT before
they are kept, PDFium's own generation, `qpdf`, and Acrobat and Firefox by a person.

**Rule 9 — both presets, and the UI binary whole** (Stage 4's I-09 and I-28).

### Severity

| Sev | Meaning |
|---|---|
| **S1** | **The product changes a byte it did not mean to** — a content stream under a redaction mark, an unmodelled key, a signed range, another annotation. **The screen shows what the file does not contain**, or the file contains what the screen never showed (D6-2). **An annotation is lost** on save, undo, redo, recovery or exchange. **An executable is opened from a document.** A crash on a corpus file |
| **S2** | A documented behaviour is wrong in a way a later stage builds on — an appearance another reader draws differently, geometry off the glyphs, a thread or status read wrongly, a merge that duplicates or drops, a migration that places an annotation on the wrong text, a chord that cannot be pressed |
| **S3** | A message is unclear, a figure is misreported, a pane mislabels, a diagnostic names the wrong thing |
| **S4** | Polish nobody outside the team would notice |

**The S1 that is new at this stage is *a change nobody asked for*.** Every earlier stage could break
the product; this one can break a document someone else will open, and the change is invisible in the
reader that made it.

---

## 2. Build, launch, environment

`cmake`, `ninja` and `g++` are **not on this machine's `PATH`**. Every command assumes
`scripts\env.ps1` has been dot-sourced first.

```powershell
. .\scripts\env.ps1
cmake --preset x64-release
cmake --build --preset x64-release --target checks      # four gates; mutation-lint names §4's commands
cmake --build --preset x64-release
ctest --preset x64-release --output-on-failure
ctest --preset x64-release -R "annot|comment|measure|fdf|migrate|stamp|attach|profile|sound"
ctest --preset x64-release -R "reversibility|roundtrip|a4_product"
.\build\x64-release\bin\rawpage_bench_pipeline.exe --stage6 > bench-stage6.txt   # T4's A2 figure — to a FILE
.\build\x64-release\bin\rawpage.exe tests\manual\M-01\annots-every-type.pdf
```

**Notes carried from `CLAUDE.md`** because they still cost time: PowerShell does not wait on a GUI
binary; its pipeline captures none of `rawpage.exe`'s output; a running `rawpage.exe` blocks the
release link; a failing run's output survives only until the next run, in
`build\<preset>\Testing\Temporary\LastTest.log`; a bench whose pipeline is cut short keeps running.

**Where things are written**, so a walker can look:

| What | Where |
|---|---|
| An annotated document | Only where **Save** or **Save As** puts it — a walk saves into `tests\manual\<case>\` |
| An exported FDF/XFDF, an extracted attachment | Where you choose — for a walk, `tests\manual\<case>\` |
| An opened non-PDF attachment | `%LOCALAPPDATA%\RawPage\attachments\<session>\`, **emptied at a clean quit** (D6-17) |
| Custom stamps | `%APPDATA%\RawPage\stamps\` (D6-16) |
| Recovery journals and their side files | `%LOCALAPPDATA%\RawPage\recovery\<docid>-<pid>-<n>\` (D6-23) |
| Settings | `%APPDATA%\RawPage\settings.ini` |
| Logs | `%LOCALAPPDATA%\RawPage\logs\` |
| A manual walk's own files | `tests\manual\<case>`, and nowhere else (`CLAUDE.md`) — a **copy** of the corpus file, never the corpus file itself |

### Settings keys that affect these cases

**Defaults are owned by `core/settings/SettingsKeys.def`** — **re-read the `.def` rather than this
table** (Stage 2's I-26). The rows and the family are listed in the plan's D6-13.

| Key | Affects |
|---|---|
| `identity/author`, `identity/email`, `identity/organization` | A-27, A-53, A-54, A-69, A-76; every walk — **set `identity/author` before M-01** so your comments carry your name |
| `commenting/stickyTool` | A-62, M-01 |
| `commenting/popupBehaviour` | A-47, M-01 |
| `commenting/default<Property>.<tool>` (the family) | A-61, A-63, M-01, M-02 |
| `measuring/scaleRatio`, `measuring/displayUnit`, `measuring/precision`, `measuring/snapToPaths`, `measuring/writeToComment` | A-87 … A-92, M-09 |
| `units/snapToObjects` | A-89, M-09 |
| `search/includeComments` | A-74 |

### Test rig

| | |
|---|---|
| **Acrobat** | **P-1** ✅ Acrobat **26.2.21931** (read 2026-10-04) — M-02, M-03, M-04, M-10 |
| **A reader that is not PDFium** | **P-2** ✅ **Firefox 157** (pdf.js) — M-02. Chrome 154 and Edge 154 are present and **are PDFium**: use them only as a control that should agree with RawPage |
| **A microphone** | **P-3** ✅ the Jabra EVOLVE 20 headset microphone — M-08 (Q5) |
| **`qpdf`** | **P-4** — CI's Ubuntu `portable-asan` job only (Q8; the `gates` job the plan first named runs no suite that writes, corrected at T3), not this machine |
| **Monitors** | Stage 4's P-1 rig (150 % and 100 %) — M-01 and M-09 ask for both, because a hit-test or a snap that holds at one scale and not the other is Stage 4's I-11 in a new place |
| **Corpus** | **97 PDFs before T1** (2026-10-04); T1 adds §5.2's fixtures, every one generated by `corpus/make-annots.py` with a `PROVENANCE.md` row and a label |

---

## 3. Verdicts this guide must produce

A stage that is green but has no verdict is not signed off. **Every scale below is fixed here, before
the measurement**, so nothing is graded against whatever it happened to achieve.

### 3.1 A2 at Stage 6

| Verdict | Assembled from | Recorded in |
|---|---|---|
| Does the frame budget hold while annotation commands reload the engine (D6-2), and does an edit reach the pixels promptly? | **Bench (T4):** A-38. **Product (T15):** A-104. **Judgement:** M-12 | [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified), labelled **Stage 6's workload** |

Two quantities, both reported, both graded:

- **The viewport's p95 queued latency** — the same quantity as Stage 2 T18's product figure and Stages
  4 and 5, so all are comparable.
- **The edit-to-pixels latency** — from `CommandJournal::run` returning to the first tile of the
  command's scope page being delivered **with the change visible in its pixels** (the case checks the
  pixels, rule 1). Reported as p95 and maximum.

The workload: one `AddAnnotation` (an ink stroke of 200 samples) every 100 ms for 30 s on page 450 of
`pages-900.pdf` and of `text-900.pdf`, and separately on `objstm-xrefstream.pdf`, with a viewport
scrolling at Stage 4's D4-16 rate. Compose time, reload time and path-geometry time (D6-14) are
reported and **not** graded.

| Result | Means |
|---|---|
| **go** | On every file, the viewport's p95 stays **inside 16.667 ms** at the shipped cache cap, **and** the edit-to-pixels p95 is **≤ 100 ms** with its maximum **≤ 250 ms** |
| **go, with named custom work** | A figure leaves its bound only under a named, reachable condition — a burst faster than one reload per frame, a cross-reference chain of many sections, a page whose re-render alone exceeds the budget — and a change in `core/render/` or `pdf/render/` brings it back: reloads coalesced to one per frame, the reload yielding to visible tiles, a scope page's re-render at `Visible`. **The work is named in the verdict and done in the stage** |
| **no-go** | No scheduling change recovers it. **D6-2 changes to the mirror** — a new decision recorded before T5, because every tool's result reaches the screen through it |

### 3.2 What is shown is what is saved (D6-2)

For **every annotation type the product creates**, created **through its tool** (not by constructing a
command): the scope page rasterized from the live engine document after the reload, and the same page
rasterized from **the saved file reopened in a fresh engine document**, at 72 and 144 dpi and under a
90° view rotation: **0 differing pixels.** The same engine drawing the same bytes must agree exactly;
any difference means the screen and the file disagree, which is an S1. After an undo, the page must
equal the original file's raster, **0 pixels**.

### 3.3 Appearance consistency (D6-6)

| Oracle | Pass bar |
|---|---|
| **Golden rasters** (`tst_appearance`) | `golden/README.md`'s thresholds, per type × property; every baseline LOOKED AT before it is kept, its look recorded in `golden/UPDATES.md` |
| **PDFium's own generation** | For highlight, underline, strikeout, squiggly, square, circle, ink and note — **eight; a line is not among them** (measured at T1, I-02): our appearance and PDFium's generated one (the same annotation with `/AP` removed), binarized at 50 % coverage at 144 dpi, overlap at **ink IoU ≥ 0.90**. **Not for a strikeout or a squiggly since T6** (I-09, plan D6-32): A-50 measured PDFium's placement of both off the glyphs, and ours is drawn on them on purpose -- those two are graded by A-50's measure against the glyphs' ink. **Text markup is graded at body-text height** (a quad 12 points tall): our underline is PDFium's 1-point line up to 14 points and grows with the text past it, on purpose (D6-32). **A note is graded as `Comment`**, the icon RawPage writes: PDFium generates its own bubble for every note whatever `/Name` says (T8, I-12) |
| **`qpdf --check`** (Q8) | *"No syntax or stream encoding errors found"* for every file the suite writes, read from the `portable-asan` job's log |
| **Acrobat and Firefox** (M-02) | Each type: the same shape, colour, opacity and position as RawPage shows, to a person comparing side by side. A difference a person would call *different* is an S2 |

### 3.4 A4 through the product — the carried exit criterion

Stage 3 (guide issue I-06, decided by the user 2026-09-14) carried **risk A4's permitted edit
through the product** to this stage.

| Half | Pass bar |
|---|---|
| **Automated (A-33, T3)** | Each of the four `corpus/signed/` files opened in the product, one comment added by the first annotation command, saved through `SaveCoordinator`: **OpenSSL verifies 4 of 4**, rejects **8 of 8** flipped-byte controls, and every original is the byte-identical prefix of its output |
| **Acrobat (M-03)** | Every **approval** signature reads *"the revision … covered by this signature has not been altered; however, there have been subsequent changes"*, and Acrobat lists the comment as that change; **`certified-nochanges.pdf` is flagged** — its certification permits no changes |

Recorded beside Stage 3's figures in `Tech-Stack-Plan.md` §3.3's A4 row, **both bounded to RawPage's
own generated signatures**.

### 3.5 Migration (D6-21)

On `annots-report-v1.pdf` → `compare/report-v2.pdf`, against `labels/annots-migration.json`:

| Measure | Pass bar |
|---|---|
| **Precision** | **100 %** — no annotation placed on a page or text other than the label's |
| **Recall** | **≥ 95 %** of the annotations the label marks placeable |
| **Reporting** | **Every** annotation the label marks unplaceable is in the report, and **none** of them is placed |
| **Position** | Text markup's quads within **1 pt** of the anchor text's glyph boxes in `report-v2` (the generator's coordinates); other annotations within **2 pt** of the label's point |

### 3.6 The Comments pane at scale

On `annots-900.pdf` (9,000 annotations), on this machine: the store hydrated from the file in
**≤ 500 ms**; the pane populated **≤ 500 ms** after; a group, sort or filter change delivered in
**≤ 100 ms at p95** over 20 changes; the store's live heap **≤ 64 MB** (measured as LIVE heap, Stage
2's I-35). A miss is an S2 and its named work is done in the stage.

### 3.7 Exchange round trip (D6-20)

For every annotation in the corpus that RawPage can write: exported to XFDF, and separately to FDF,
then imported into a copy of the document with every annotation removed: **every modelled field
equal** — the fields the labels carry — with dates compared at the second, PDF's precision.

---

## 4. Traceability — exit criterion → case

| Exit criterion ([`Implementation-Plan.md` § Stage 6](./Implementation-Plan.md#stage-6--annotations-and-review)) | Closed by |
|---|---|
| Every §4.1 tool creates a correct, correctly-appearing annotation | A-34, A-49 … A-66, A-75 … A-81, A-85, A-87 … A-92, A-100, A-101, A-108; **M-01**, **M-02**, **M-06**, **M-07**, **M-08**, **M-09** |
| The Comments pane groups, filters, sorts, searches, replies and sets status | A-67 … A-74; **M-05** |
| FDF/XFDF round-trips with at least one other reader | A-93 … A-97 on generated files; **M-04** with Acrobat |
| Comment summaries generate in all four layouts | **Moved to Stage 8 by Q4, approved 2026-10-04** (D6-19) — no case here |
| Annotations undo cleanly | A-25, A-28, A-35, A-42, A-44, A-51, A-60, A-71, A-95, A-107; **M-11** |
| *(Carried from Stage 3)* A4's Acrobat half through the product | **A-33**, **M-03**, graded on §3.4 |
| *(Plan D6-1)* Every change a command, one gesture one step | A-08, A-09, A-28, A-29, A-42, A-44, A-71 |
| *(Plan D6-2, D6-24)* Shown is saved; A2 at Stage 6 | A-34 … A-39, A-104, A-108; **M-12** |
| *(Plan D6-3)* One model, read first-party, agreeing with the engine | A-17 … A-19 |
| *(Plan D6-4, D6-5)* Identity; nothing unmodelled lost | A-26, A-27, A-29 |
| *(Plan D6-6)* Appearances checked by oracles that are not the builder | A-31, A-64 … A-66; **M-02** |
| *(Plan D6-7, D6-8)* Fonts; typewriter as an annotation | A-55 … A-57; **M-06** |
| *(Plan D6-9)* Chords that can be pressed; the context chain | A-40, A-44 … A-46, A-70 |
| *(Plan D6-10, D6-11)* Precise invalidation; comments searched from the store | A-36, A-74 |
| *(Plan D6-12)* Threads, status, checkmarks as ISO's | A-21, A-68, A-69 |
| *(Plan D6-13)* The keys, the family, three pages | A-54, A-61, A-62, A-92 |
| *(Plan D6-14)* Measurement | A-87 … A-92; **M-09** |
| *(Plan D6-15)* Ink and the eraser | A-59, A-60 |
| *(Plan D6-16)* Stamps, from images (a PDF page source is Stage 7's, Q7) | A-75 … A-80; **M-07** |
| *(Plan D6-17)* Attachments | A-82 … A-86; **M-13** |
| *(Plan D6-18)* Audio | A-07, A-101; **M-08** |
| *(Plan D6-20, D6-21)* Exchange and migration | A-93 … A-99; **M-04**, **M-10** |
| *(Plan D6-22)* Copy, cut and paste | A-81 |
| *(Plan D6-23)* Large payloads in recovery | A-86, A-107 |
| *(Plan D6-25)* Redaction marks remove nothing | A-100 |
| *(Plan D6-27, D6-28)* Flags; pop-ups | A-43, A-47 |
| *(Plan D6-29)* Hostile input | A-22, A-23, A-97 |

---

## 5. Automated verification — I run this

Every case names its command and its pass condition. I report the **actual** output; a failing case is
reported as failing, with that output, and is never declared fixed without a re-run. **When a case is
ticked it carries the evidence it was ticked on** — Stage 1's I-32 was a guide whose summary claimed a
green its cases did not record. Every *"byte-unchanged"* diff below is taken against **`b49b13c`**,
the tree at Stage 5's sign-off.

### 5.1 Build, boundary, environment

**A-01 · Configure and build, both presets, from clean** ✅ *2026-10-20, T15*
**Steps** `cmake --preset x64-debug`, `--preset x64-release`; `cmake --build --preset <p> --clean-first` for each, the log kept.
**Expect** Exit 0 on all four; **0 `warning:` and 0 `error:` lines in either log**, `ui/` included. `--clean-first` is required (Stage 3's A-01). Object counts re-derived, not quoted. **If it fails** S1.
**Result** `cmake --preset` and `cmake --build --preset <p> --clean-first` for `x64-release` and `x64-debug`, the logs kept: exit 0 on all four; **0 `warning:` and 0 `error:` lines in either log, over 434 C++ objects on each** (the logs' `Building CXX` lines, 433 at T14 -- T15's one new source, `tst_shownsaved.cpp`); the release build 224 s, the debug build 462 s.

**A-02 · The `ui/` warning grace list is still empty** ✅ *2026-10-20, T15*
**Steps** Read `RAWPAGE_UI_WARNING_GRACE` in `cmake/RawPageWarnings.cmake`.
**Expect** Empty. **If it fails** S3, and the entry's reason is recorded in the status block.
**Result** `RAWPAGE_UI_WARNING_GRACE` in `cmake/RawPageWarnings.cmake` holds no entry -- the comment Stage 0 left in it and nothing else; `ui/` builds under `-Werror` with no exception (A-01's 0 warnings).

**A-03 · The headless suite is still headless** ✅ *2026-10-20, T15*
**Goal** The annotation model, the store, `core/Measure`, the codecs and the migrator are `core/`, and `core/` links no display and no engine.
**Steps** `ctest -R "gates.headless_assert"`.
**Expect** Every case as before, the negative controls still caught. **If it fails** S1.
**Result** The seven `gates.headless_assert.*` cases green in the `x64-release` run of 1816 and in the `gates` run of 137 -- `clean_closure_is_allowed` and `ops_edge_is_allowed`, and the five whose bad closures must be caught: `direct_pdfium_edge`, `transitive_pdfium_edge`, `pdfium_name_without_target`, `ops_carrying_pdfium_is_caught`, `ui_edge_still_caught`. And `rawpage_tests` itself configured and built on both presets (A-01) -- `rawpage_assert_headless()` fails configure otherwise -- with the annotation model, the store, `core/Measure`, the codecs, the migrator and `SoundData` in it.

**A-04 · `portable-asan` runs every Stage 6 suite in `core/`, `pdf/cos/` and `pdf/ops/`** ✅ *the log read 2026-10-20 at T15, CI run 39267069191 on `bc2bb44` -- T15's two new `tst_redaction` cases owed to the first log after T15 is pushed*
**Steps** The `portable-asan` CI job; **read the log, not the tick** (rule 3).
**Expect** Every case of `tst_annotation`, `tst_annotstore`, `tst_annotationops`, `tst_measure`, `tst_fdf`, `tst_migrate`, `tst_profilestore`, `tst_sounddata` and every `[serialize:]`/`[revert:]` case present **by case name**; **0 `runtime error:`, 0 `ERROR: AddressSanitizer`, 0 `LeakSanitizer` lines**. **What it does not cover:** PDFium, the reload (D6-2) and every `ui/` surface. **If it fails** S1 for a sanitizer finding; S2 if a suite is absent.
**Result so far** *read 2026-10-09:* every `portable-asan` log from T3's push to T10's has been green, the last three read whole -- runs 37756191062 (T8, 881 cases), 37833779103 (T9, 894) and 37907611236 (T10, **914**) -- with **0 `runtime error:`, 0 `ERROR: AddressSanitizer` and 0 `LeakSanitizer` lines**; T10's log runs `tst_annotation`'s, `tst_annotstore`'s, `tst_annotationops`' and `tst_profilestore`'s cases and the two-process `unit.profile_store.restart` by name, and every `[serialize:]` and `[revert:]` case with them. **Open until their suites exist:** `tst_measure` (T12), `tst_fdf` and `tst_migrate` (T13) and `tst_sounddata` (T14).
**Result at T15 -- the suites T12, T13 and T14 added, read from the log.** CI run **39267069191** on `bc2bb44` (Stage 6 guide I-22's fix, the last push before T15), its `portable-asan` job read whole with `gh run view --log`: *"100% tests passed, 0 tests failed out of 991"*, **0 `runtime error:`, 0 `ERROR: AddressSanitizer`, 0 `LeakSanitizer` lines**, and the suites that were open run by case name -- `tst_measure`'s, `tst_fdf`'s XFDF and FDF cases, `tst_migrate`'s, `tst_sounddata`'s (*"a PCM WAV of every depth and channel count round-trips its samples exactly"*, *"a sound comment is saved as a /Sound stream and read back byte for byte"*) -- beside `tst_annotation`'s, `tst_annotstore`'s, `tst_annotationops`' and `tst_profilestore`'s, and the serialize and revert cases by name (*"a lock serializes the bits it sets …"*, *"a move serializes its ids its offset and its instant …"*); A-31's step in the same job: *"69 file(s), 0 failed; the truncated-stream control rejected"*. **Owed:** T15 adds two cases to `tst_redaction` (A-26, A-27/A-32), whose first sanitizer run is the first CI run after T15 is pushed.

**A-05 · The product starts, and every new key resolves** ✅ *2026-10-20, T15*
**Steps** `$env:QT_QPA_PLATFORM="offscreen"`; `rawpage.exe --dump-ui-strings`, `--lang zh-TW --dump-ui-strings` and `--list-actions`, **through `cmd` redirection**.
**Expect** Exit 0; every `annot.*`, `comments.*`, `measure.*` and `stamps.*` key resolves in `en` and `zh-TW` and **none resolves to itself**; the log says the backend is **4 of 7** and the platform **6 of 18** — Stage 6 adds no interface (plan §5.1). **If it fails** S2.
**Result** Under `QT_QPA_PLATFORM=offscreen`, through `cmd` redirection: `--dump-ui-strings`, `--lang zh-TW --dump-ui-strings` and `--list-actions` each exit 0. **225 keys of Stage 6's families** -- `action.annot.*` and the families it minted, `comments.`, `stamps.`, `measure.`, `attachments.`, `audio.` (no `annot.` family exists: A-11) -- **resolve in `en` and in `zh-TW`, none to itself**; the log says *"backend: assembled -- 4 of 7 interfaces implemented; missing: ISigner, IOcrEngine, IIndexer"* and *"platform: Windows bundle assembled -- 6 of 18 interfaces implemented"* -- Stage 6 adds none.

**A-06 · Stage 6's `core/` uses only the Qt the portable presets have** ✅ *the log read 2026-10-19 from CI run 38926974240's `portable-asan` job (on `f11d17b`, after T14): on ubuntu-24.04, "Setting up libqt6core6t64:amd64 (6.4.2+dfsg-21.1build5)", then `rawpage_core`'s `doc/AnnotationStore.cpp.o`, `doc/XfdfCodec.cpp.o`, `doc/CommentMigrator.cpp.o`, `doc/SoundData.cpp.o` and `Measure.cpp.o` built, and the job green, 990/990*
**Goal** Convention 24: CI builds `core/` against Ubuntu's Qt **6.4**.
**Steps** The `portable-asan` job's build log.
**Expect** `core/doc/` (the store, `XfdfCodec` over `QXmlStreamReader`, `CommentMigrator`, `SoundData`) and `core/Measure` compile there. **If it fails** S2.

**A-07 · Audio deploys Media Foundation and nothing of FFmpeg** ✅ *2026-10-16, T14 (Q5)*
**Steps** `objdump -p build\x64-release\bin\rawpage.exe`; the deployment step's output directory; `license-audit` as `checks` runs it.
**Expect** `Qt6Multimedia.dll` imported by `rawpage.exe` and no `core/` library; `plugins\multimedia\windowsmediaplugin.dll` deployed; **no `ffmpegmediaplugin.dll`, `avcodec-*`, `avformat-*`, `avutil-*`, `swresample-*` or `swscale-*`** anywhere beside the binary; `license-audit`'s Qt-module list names `Multimedia` and it is clean. **The control:** copying `ffmpegmediaplugin.dll` into the deployment makes the case fail. **If it fails** S2.
**Result** `ui.deployment_media_foundation_only` (`tests/gates/CheckMultimediaDeploy.cmake`, a `cmake -P` case in the x64 presets -- it judges files on disk and two PE import tables, which no test binary links): *"Qt6Multimedia.dll and multimedia/windowsmediaplugin.dll deployed, nothing of FFmpeg; rawpage.exe imports Qt Multimedia and rawpage_tests.exe does not; qt-modules.txt names it"*, on both presets; `checks`' `license-audit` reads *"5 Qt modules"*, clean. **A deviation from the Expect as drafted:** the plugin is deployed at `multimedia\windowsmediaplugin.dll` beside the executable -- windeployqt's layout, a directory Qt searches as an application library path -- not under `plugins\`. Its CONTROL, `ui.deployment_media_foundation_only_control`, plants the real `ffmpegmediaplugin.dll` in a scratch copy of the deployment and requires the check to name it; and planted **P8** put it in the real `bin\multimedia\` and the case FAILED, passing again once it was removed. **What the files cannot say -- which plugin a running process LOADS** -- is `ui.composition.the backend this process loads is Media Foundation and never FFmpeg` (`tst_audioproduct`, `[A-07]`): after the first device query, *"windowsmediaplugin.dll D:\...\build\x64-release\bin\multimedia\windowsmediaplugin.dll; ffmpegmediaplugin.dll not loaded"*, `QT_MEDIA_BACKEND` set to `windows` by the product (`ui::useMediaFoundation`, D6-41(j)) -- needed because Qt 6 PREFERS FFmpeg and a developer's PATH reaches the Qt prefix, which holds both. Planted **P9** (the backend not pinned) failed it.

### 5.2 The gates — and the numbers that must and must not move

**A-08 · `checks` clean, and `mutation-lint` names exactly the plan's commands** ✅ *2026-10-20, T15* *(Q2)*
**Goal** D6-1's mechanical proof.
**Steps** `cmake --build --preset x64-release --target checks`; `mutation-lint.exe --root . --explain`.
**Expect** Exit 0; four gates; `mutation-lint` names **exactly** `RotatePageCommand` and the plan §4's commands — **twelve**, Q2 approved 2026-10-04 — each with its `Commands.def` row and its `[serialize:]` and `[revert:]` cases listed by `--explain`. **If it fails** S1 for an unlisted command; S2 for a missing test.
**Result** `cmake --build --preset x64-release --target checks`: exit 0, the four gates -- *"layer-lint: clean -- 662 files, 6693 includes"*, *"license-audit: clean -- 20 components, 5 Qt modules, 2 warning(s)"* (the two Apache-2.0 NOTICE warnings for components this configuration does not build, as at every stage), *"i18n-lint: clean -- 414 files, 2 catalog(s), 0 waiver(s)"* and *"mutation-lint: clean -- 12 command(s): AddAnnotationCommand DeleteAnnotationCommand EditAnnotationCommand MoveAnnotationCommand SetAnnotationAppearanceCommand LockAnnotationCommand ReplyToAnnotationCommand SetAnnotationStatusCommand ImportAnnotationsCommand AddAttachmentCommand DeleteAttachmentCommand RotatePageCommand; 12 table row(s)"*. **Exactly** `RotatePageCommand` and the plan §4's eleven (*"Commands -- eleven"*: nine annotation commands and two attachment commands) -- twelve, each with its `Commands.def` row and its `[serialize:]` and `[revert:]` cases listed by `--explain`.

**A-09 · `mutation-lint` still fires** ✅ *2026-10-20, T15*
**Steps** A throwaway `core/cmd/commands/ZzProbeCommand.h` deriving from `Command`, with no row and no tests; `mutation-lint`; delete it.
**Expect** Fails naming **that** class, three times (row, serialize, revert); clean again after. **If it fails** S1.
**Result** `core/cmd/commands/ZzProbeCommand.h` -- a `final` class deriving from `Command`, nothing else -- and `mutation-lint --root .` **exit 1**, *"mutation-lint: 3 finding(s)"*: `unregistered-command: ZzProbeCommand derives from Command and has no RAWPAGE_COMMAND row in core/cmd/Commands.def`, `no-serialize-test` and `no-revert-test`, each naming the class and its line. Deleted: clean again, twelve commands.

**A-10 · The waiver census stays FIFTEEN, and the engine stays in `pdf/render/`** ✅ *2026-10-20, T15*
**Steps** `layer-lint.exe --root . --explain`.
**Expect** Clean; **15** active filesystem-write escapes, the same fifteen as at Stage 5's sign-off — the extract, the exports and the attachment session folder are written by `pdf/` (D6-17, D6-20), the recovery side files by `RecoveryJournal`'s existing write (D6-23), the stamps by `core/settings/` (D6-16). `FPDF_` appears only in `pdf/render/` — the reload (D6-2) included. A sixteenth escape is a finding. **If it fails** S2.
**Result** `layer-lint.exe --root . --explain`: clean, *"662 files, 6693 includes, 15 escapes"* -- the same fifteen as at Stage 5's sign-off, the last of them `ui/panes/SearchPane.cpp`'s CSV export (D5-13); `fpdf`, `fpdf-handle`, `argv-path-read` and `bare-qaction` with no escape -- so `FPDF_` only in `pdf/render/`, the reload (D6-2) included. Stage 6's writes -- the extracts, the exports, the attachment session folder, the side files, the stamps -- went through `pdf/`, `RecoveryJournal`'s existing write and `core/settings/`, as D6-17, D6-20, D6-23 and D6-16 planned.

**A-11 · `i18n-lint` knows `annot`, `comments`, `measure` and `stamps`** ✅ *2026-10-20, T15*
**Steps** `i18n-lint.exe --root . --explain`; a throwaway source naming `annot.noSuchKey`.
**Expect** The four families listed; the probe fails `unknown-key` naming **that** key; every Stage 6 key in both shipped catalogs; **no catalog entry outside its `<context>`** (Stage 5's I-05). **If it fails** S2.
**Result** `i18n-lint.exe --root . --explain` lists the families `action.* shell.* ribbon.* prefs.* error.* command.* panes.* search.* compare.* comments.* stamps.* attachments.* measure.* audio.*` -- **five of them Stage 6's: `comments`, `stamps`, `measure`, `attachments`, `audio`** -- and both shipped catalogs at **1374 keys** (`zh_TW` 29 marked unfinished, Stage 15's review). **A deviation from the Expect as drafted: there is no `annot.` family**, because no catalog key begins `annot.` -- Stage 6's annotation strings are `action.annot.*` (the action table's), `shell.`, `error.` and `prefs.` -- so a source naming `annot.noSuchKey` is a string the gate does not read as a key, which the family rule states. **The probe was run on a Stage 6 family instead**: `ui/ZzProbe.cpp` returning `QStringLiteral("comments.noSuchKey")`, and `i18n-lint` **exit 1**, *"unknown-key: comments.noSuchKey is named by this source and defined by no catalog"*; deleted, clean again. `wrong-context` (Stage 5 I-05) is part of the clean run: no catalog entry outside its `<context>`.

**A-12 · No dependency arrived without a decision** ✅ *2026-10-20, T15*
**Steps** `git diff b49b13c..HEAD -- vcpkg.json`; `license-audit`; `objdump -p` for imported DLLs.
**Expect** `vcpkg.json` unchanged; `license-audit` clean with Stage 5's component count; **no `icu*`, `harfbuzz`, `freetype` or `hunspell` DLL imported**; Qt Multimedia, by Q5 (approved 2026-10-04), and nothing else (A-07). **If it fails** S2.
**Result** `git diff b49b13c..HEAD -- vcpkg.json`: empty. `license-audit`: clean, **20 components** -- Stage 5's count -- and **5 Qt modules** against Stage 5's 4, the fifth Multimedia (Q5). `objdump -p build\x64-release\bin\rawpage.exe`: Qt6Core, Qt6Gui, Qt6Widgets, **Qt6Multimedia**, pdfium, libjpeg-62, libopenjp2, libz, the GCC runtime, msvcrt and Windows' own DLLs -- **no `icu*`, `harfbuzz`, `freetype`, `hunspell` or FFmpeg DLL** imported, and none of them in `bin\` (A-07 for the deployment).

### 5.3 The corpus this stage is judged on (T1)

**A-13 · The new files regenerate byte-identically, and nothing old moved** ✅ *2026-10-05, T1*
**Steps** `python corpus/make-corpus.py --verify`, `--check-provenance`, `--self-test`; `git status corpus/`.
**Expect** All three exit 0; one `PROVENANCE.md` row per new file naming what it carries **and its control** (plan §5.2); **every pre-existing corpus file byte-unchanged**. **If it fails** S2.
**Result** `--verify`: *"152 files regenerate byte-identically"* — the 121 of Stage 5's sign-off plus 31 (11 PDFs, 12 labels, 6 exchange files, 2 images). `--check-provenance`: *"116 PROVENANCE.md rows, one per file; 22 labels and 14 comparison records name files that exist"* — rows now cover `.fdf`, `.xfdf`, `.png` and `.jpg` as well as `.pdf`, and every new row names its control. `--self-test`: *"14 of 14 controls fired"*, the four new ones each matched to the file it damaged. `git status corpus/`: no pre-existing data file modified — **and `--verify` passed over the 121 earlier files after each change to `deflate.py` and `make-compare.py`** (I-01; `render_page` gained a recorder that draws nothing). The first fix for I-01 DID move `ordinary/image-flate-predictor.pdf`, which is how the kept-when-complete rule was found.

**A-14 · The labels are data, checked like the corpus** ✅ *2026-10-05, T1*
**Steps** `--verify` and `--check-provenance` over `corpus/labels/annots-*.json`.
**Expect** Every label regenerates byte-identically and names a corpus file that exists; every annotation fixture has one. **The control:** a label naming a missing file fails. `.gitattributes` treats the labels as text (Stage 5's A-13 finding). **If it fails** S2.
**Result** Twelve labels — one per annotation fixture (`annots-<name>.json`, `annots-attachments.json` and `annots-measure-scale.json` for the two whose names do not start `annots-`) and `annots-migration.json` — all within `--verify`'s 152, all resolving; the migration label's `revised` file is checked too. Controls: *"label for no file is LABEL"* and the new *"annotation fixture, no label"* (`labels/annots-threads.json` removed → `UNLABELLED ordinary/annots-threads.pdf`) both fire. `git check-attr` on `corpus/labels/annots-every-type.json`: `text: set`, `diff: set`, `eol: lf` — the Stage 5 rule covers them.

**A-15 · Each fixture carries what its row claims — read from the BYTES** ✅ *2026-10-05, T1*
**Goal** A fixture that does not carry its fault tests nothing (Stage 3's A-17).
**Steps** `make-corpus.py --check-faults` over the new files, decoding streams with `zlib` and never through `pdf/cos/` or PDFium.
**Expect** `annots-every-type.pdf` has every subtype the plan lists, each with `/AP`; `annots-no-ap.pdf`'s page 1 has **no** `/AP` on any annotation; `annots-threads.pdf` has `/IRT` chains, `/RT /Group`, `/StateModel /Review` and `/Marked` replies, the cycle, the orphan and the cross-page reply; `annots-flags.pdf` each flag bit; `annots-direct.pdf` a direct dictionary and an annotation in two pages' `/Annots`; `annots-unknown-keys.pdf` `/PieceInfo`, `/RawPageTestKey` and `/RC`; `attachments.pdf` its three embedded files with `/Desc` and a `FileAttachment`; `measure-scale.pdf` `/Measure` dictionaries; each FDF and XFDF its claimed shape, the malicious XFDF its external entity. **Two controls:** an `/AP` added to `annots-no-ap.pdf`, and `/RawPageTestKey` removed, each fail the check. **If it fails** S2.
**Result** `--check-faults`: *"every Stage 5 and Stage 6 fixture carries what its row claims"*, through `corpus/fixturecheck.py`'s `check_annots` — object streams expanded (`annots-900.pdf` counts **9,000** annotation dictionaries), and **every `labels/annots-*.json` entry held to the object it names** (its `/Subtype` and `/NM`), so a label cannot describe an annotation the file lacks. Beyond the Expect: the exchange files' `/F`, `<f>` and `/ID`/`<ids>` match their targets; `malformed.xfdf` must FAIL to parse; the PNG is colour type 6 with alpha 0, 255 and between; the JPEG is SOF0. **Both controls fire, each on its own file:** *"FAULT ordinary/annots-no-ap.pdf -- page 1 must hold nine annotations and NONE with /AP (9, 1 with)"* and *"FAULT ordinary/annots-unknown-keys.pdf -- /RawPageTestKey is missing"*. One check was wrong on its first run and fixed in the checker, not the fixture: the shared-annotation test's lazy regex stopped at the first `]` inside a direct dictionary.

**A-16 · The golden manifest knows the new files** ✅ *2026-10-05, T1*
**Steps** `golden-update.exe --check-manifest`.
**Expect** Every new PDF a case or an excluded row with a reason; baselines captured with **no tracked PNG changed**; `--explain`'s census re-derived. **If it fails** S3.
**Result** *"108 corpus files, 103 with baselines and 5 excluded with a reason"* — all eleven new PDFs are cases (18 pages). **Every page was captured into a scratch golden root and LOOKED AT first**, and three fixtures were corrected from what the pictures showed: a caret drawn over the "w" of *words*, a reply icon over the line above its highlight, and two measurement readouts overprinting at the rectangle's centre. The same look found **I-02**. Then **54 baselines in one filtered run**, each filter naming one new file (`annots-` alone would have re-captured `annots-mixed`): `git status golden/` shows **no tracked PNG modified**, and `captured.json` lost only its header's date and reason. `golden/UPDATES.md` carries the one entry.

### 5.4 The model and its reading (T2)

**A-17 · The reader returns the generator's annotations** ✅ *2026-10-05, T2*
**Goal** Rule 7.
**Steps** `tst_annotreader` over every labelled fixture.
**Expect** For every annotation: page, subtype, rectangle, quadpoints, vertices, ink samples, colour, opacity, border, `/Contents`, `/Subj`, `/T`, `/NM`, dates, `/IRT`, `/RT`, state and flags **equal to the label**; none missing, none invented. **The control:** one label entry removed surfaces as an invented annotation. **If it fails** S1 for an invented or lost annotation; S2 for a wrong field.
**Result** `render.the reader returns every labelled annotation field by field`: *"11 labelled files, 151 listed annotations compared field by field"*, through the SHIPPED backend, every one equal -- beyond the Expect's fields also the line, callout, `/RD`, interior colour, intent, icon, `/DA`, `/RC` (holding the label's text XML-escaped), pop-up and parent, `/Open`, `/Rotate`, `/AP` present, the identity (object number, or page and position for a direct dictionary), and both dates in UTC. `annots-900.json`'s three written pages included. **The control fires:** the label with its last page-1 entry removed reports *"1 annotation(s) INVENTED"*. A third case holds the shared annotation of `annots-direct.pdf` to be ONE annotation listed twice and owned by the page its `/P` names. Planted control P5 (the author read as raw bytes) failed this case on `Zoë Müller`; P1 (ownership by first listing) did NOT fail the shared-annotation case -- that fixture's `/P` IS its first listing page -- and failed A-22's thousand-page case instead.

**A-18 · The two readers agree, and a page where they do not is read-only** ✅ *2026-10-05, T2*
**Goal** D6-3.
**Steps** Over every corpus file both readers open: the first-party store against `FPDFAnnot_GetCount`, `_GetSubtype` and `_GetRect` per page; then a planted disagreement (the reader told to skip one annotation on page 2 of `annots-mixed.pdf`).
**Expect** Agreement on every page of every file, the rectangle within `float` tolerance; under the plant, page 2 is **read-only** — `Permissions::may(Operation::Annotate)` refuses it by key with the page named — and page 1 is editable. **If it fails** S2.
**Result** *"98 files both readers read, 3014 pages, 9147 /Annots entries; 0 page(s) disagree"*. **Deviation in the plant: it is on `annots-threads.pdf` page 2**, because `annots-mixed.pdf`'s page 2 carries no annotation; with one entry removed from the first-party reading, `mayAnnotate(1)` refuses with `error.edit.annotationsDisagree` and context `2`, page 1 and `may(EditPages)` stay allowed. The predicate is `mayAnnotate(page)`, not `may(Operation::Annotate)`, which answers for the whole document. **Beyond the Expect, three more cases:** (a) TEN awkward `/Annots` forms built in memory -- no `/Rect`, five numbers, reversed, a name inside, an indirect `/Rect`, a reference to no object, a direct dictionary, an integer object, an unknown subtype, no subtype -- read alike by the engine and the first-party reader ENTRY BY ENTRY, which is what the reader's "read `/Rect` as PDFium does" rests on; (b) a page whose `/Rect` holds a number too long for a double, which the two readers DO read differently (our lexer clamps it), opened through the backend: that page read-only, the other editable -- the backend's own wiring, which no corpus file can show; (c) `tst_permissions`' per-entry plants (subtype, a rectangle 0.5 pt off, no rectangle, not a dictionary), each refusing exactly that page, and a 4e-6 pt difference accepted. Planted controls P4, P6, P7, P9 and P10 each failed the case written for them.

**A-19 · What cannot be read is refused by name** ✅ *2026-10-05, T2*
**Steps** Open every `corpus/encrypted/` file; and `annots-direct.pdf`'s annotation with no `/P` listed by no page.
**Expect** An encrypted document: the store refuses by key (the reason D2-6) and no annotation command is enabled; the unplaceable annotation is **reported** in the log and absent from the store. **If it fails** S2.
**Result** All five `corpus/encrypted/` files: `status()` is `error.edit.encrypted`, the store empty, `may(Operation::Annotate)` and `mayAnnotate(0)` refused -- *no command is enabled* is the predicate's half; no annotation command exists until T3. **The fixture was amended for this case:** `annots-direct.pdf`'s no-`/P` annotation was LISTED by page 2, so it was placeable; T2 added an annotation with no `/P` that no page lists, reached only through the `/IRT` of a hidden state reply (neither is drawn: the golden suite passed unchanged over it). The log carries *"object N is an annotation no page lists; it has no page and is ignored"*, the store has no annotation at that object, and the reply that points at it is noted `MissingParent`.

**A-20 · The model's invariants** ✅ *2026-10-05, T2*
**Steps** `tst_annotation`.
**Expect** A rectangle is normalized (`x0 ≤ x1`, `y0 ≤ y1`); quadpoints keep ISO's corner order and the de facto order Acrobat writes is read; colour arrays of 0, 1, 3 and 4 components; flags round-trip; a JSON serialize → deserialize is identity for every subtype. **If it fails** S2.
**Result** `tst_annotation`, nine cases, all green: a reversed `/Rect` normalized and a non-finite corner refused by `normalizedRect`; Acrobat's and ISO's corner orders each read AS WRITTEN; colours of 0, 1, 3 and 4 components; flags 28 and 642 named; dates across `+08'00'`, `Z`, `-05'00'`, a missing closing apostrophe, `D:2026` and `D:202609` to one UTC instant, and four non-dates kept with no instant; identities through their strings; every subtype name both ways; the JSON round trip for every subtype with every field set (an `/IRT` of 2^60 kept exactly -- object numbers are 64-bit integers in the JSON, never doubles) and for **every annotation of the eleven labelled files** -- more than 9,000, the case asserts.

**A-21 · Threads, states and queries are the label's** ✅ *2026-10-05, T2*
**Steps** `tst_annotstore` over the labels of `annots-threads.pdf`, headless.
**Expect** Every reply under its parent; the group shown as one item; each annotation's status and **history** (author and date of each state reply) equal to the label; checkmarks **per author**; the `/IRT` cycle broken and **reported**, the orphan and the cross-page reply top-level and reported. **If it fails** S2.
**Result** `tst_annotstore`, six A-21 cases over `annots-threads.pdf` read headless, graded on its label: every reply under its parent (a reply to a reply found); the replace edit's StrikeOut a group member of its Caret and not a comment under it; the four histories -- two Review, two Marked -- equal in author, state, UTC date and object, in time order across four zones, and the CURRENT review the latest on every annotation with a history; the checkmark per author (Zoë's latest `Unmarked` is not marked, Ana's `Marked` is, Ben never marked); both cycle members, the missing-parent reply and the cross-page reply top-level, each with its note; the whole top level equal to the label's roots plus those four; queries by author, text (case-insensitive), page and date order, and Hidden and NoView excluded on request (`annots-flags.pdf`, 2 fewer). Planted controls P2 (the cycle not broken) and P3 (the current review the FIRST) failed it -- **P3 only after the case was strengthened**: it had checked one annotation whose history has one entry, where first and latest coincide.

**A-22 · Hostile annotations are bounded** ✅ *2026-10-05, T2*
**Steps** `tst_annotstore`'s and `tst_annotreader`'s hostile cases: NaN and infinite rectangles, a 1,000,000-point `/InkList`, a 10,000-deep `/IRT` chain, `/Annots` that is a dictionary, an annotation listed by 1,000 pages.
**Expect** Each read within one second, bounded memory, the defect reported; no crash. **If it fails** S1 for a crash or a hang; S2 otherwise.
**Result** Built in memory (`tests/unit/TinyPdf.h`), read and threaded: the 1,000,000-point `/InkList` in **205-230 ms on `x64-release`** (1.8 s on `x64-debug`, where the case grades a five-second ceiling and says why -- the one-second bound is the shipped build's -- and 2571 ms under `portable-asan`, which the second failed until it was given a ten-second ceiling of its own, I-05), truncated to `kMaxCoordinates` (200,000) and noted; the 10,000-deep chain in 26 ms release (~250 ms debug), one top-level item and a parent chain 10,000 deep, walked without recursion; the 10,000-member cycle in 24-25 ms (~250 ms debug), every member top-level and noted; `/Annots` as a dictionary: nothing listed, noted; one annotation on 1,000 pages: one annotation, 1,000 listings, owned by page 501 (its `/P`), 999 notes, 2-3 ms (35 ms debug). **"NaN and infinite rectangles" cannot be produced through `pdf/cos/`**: PDF syntax has no NaN, and the lexer CLAMPS a number too long for a double (Stage 2's choice) -- so the case holds that a 400-digit coordinate reaches the model finite and clamped, and the model's own refusal of a non-finite corner is A-20's; the reader's `NonFiniteRect` branch is defensive and said to be. Planted P8 (the bound lifted) failed it.

**A-23 · Five fuzz targets, and the fifth runs** ✅ *Windows half 2026-10-05, T2; the Linux logs read 2026-10-08: CI run 37655927348's `portable-fuzz` (all five linked, `fuzz_annot_pipeline` "Done 20570 runs in 61 second(s)") and soak run 37605929140 on `7836913` (five targets, `fuzz_annot_pipeline` "Done 19356 runs in 601 second(s)", no finding) -- the soak predates T7's builder. **Soak run 38384043765 on `3194c8c` (T11), 2026-10-13: `fuzz_annot_pipeline` FOUND I-17** after 12,564 runs (the JSON invariant), the other four targets clean; closed 2026-10-14, its regression `[I-17]` in `tst_annotstore`; the soak over the fix is the next nightly run's reading*
**Steps** `ctest --preset gates -R fuzz_count`; the `portable-fuzz` CI job's **log**.
**Expect** The count assertion at **5**, its controls refusing 4 and 0; `fuzz_annot_pipeline` *"Done N runs in 6x second(s)"* with no finding, and in the soak. **If it fails** S2.
**Result so far** `tests/fuzz/CMakeLists.txt` expects **5**; `gates.fuzz_count.five_of_five_with_a_helper` passes and `four_of_five_fails` refuses, beside `zero_fails` -- `gates` **135/135**. The body (`tests/fuzz/AnnotPipelineFuzzBody.h`) replayed by `tst_annotstore` over every corpus PDF: *"108 corpus files, 101 walked, 9146 annotations through the body"*, every invariant held; its control (a JSON writer that drops a field) is caught by the equality the body uses. `ci.yml`'s `portable-fuzz` job and `fuzz-soak.yml` run the fifth target, seeded with `corpus/ordinary` and `corpus/malformed`. **The job's log, CI run 37250978598 on `133a046`:** the count *"(5, as expected)"*, and `fuzz_annot_pipeline` *"Done 18589 runs in 62 second(s)"* with no finding. **Not yet read:** the soak's log.
**At T13** (D6-29): `fuzz_annot_pipeline` gained an EXCHANGE branch, run first on every input (`exchangeFuzzOnce`, `tests/fuzz/AnnotPipelineFuzzBody.h`) -- the bytes read as an exchange file (`FdfCodec::readEither`), each annotation it holds given a NAME OF ITS OWN, written back as XFDF and as FDF and read again to the same count, and merged into a page of a real document as one `ImportAnnotationsCommand` the store accepts -- and both CI's `portable-fuzz` job and the soak seed it with `corpus/exchange/` too. Replayed headless by `unit.the annotation fuzz body's exchange branch holds over every exchange fixture and its truncations` (`tst_fdf`): *"A-23 exchange replay: 6 files and their truncations, 88 annotations read whole"* -- known inputs find nothing new by construction (convention 19); this proves the branch runs and agrees with them. Writing its named invariant found **I-20** before any run. **Its Linux logs, read 2026-10-19:** CI's `portable-fuzz` over T13 and T14 linked and ran all five targets, and soak 38871661378 on `a8c9221` ran the branch clean (27,928 runs); the first soak over T14, 39226515635 on `7f018f9`, found **I-22** in it -- an FDF we wrote read back by the repair scan -- and 39267194087 on `bc2bb44` ran clean over its fix (15,802 runs).

### 5.5 The write path (T3)

**A-24 · An update holds exactly what the edit needs** ✅ *2026-10-05, T3*
**Goal** D3-5 applied to annotations.
**Steps** `tst_annotationops`: add a highlight to page 1 of `text-a4.pdf`, save; diff the objects of the update against the original's.
**Expect** The update holds **exactly** the new annotation dictionary, its `/AP` stream (and its form resources if any) and page 1's dictionary or its `/Annots` array — **nothing else**; the original is the byte-identical prefix of the saved file. **If it fails** S1 for an object that should not be there.
**Result** `tst_annotationops`: one highlight on page 1 of `text-a4.pdf` composes an update of **exactly three objects** -- page 1's dictionary, the annotation (`/Type /Annot /Subtype /Highlight`, its `/P` page 1) and its form XObject -- read back from the saved file, whose first bytes ARE the opened file. Beyond the Expect: (a) a page whose `/Annots` is an indirect array nobody else lists gets the ARRAY OBJECT replaced and its page dictionary untouched (a file built in the test -- no corpus file has one); (b) a rotation and an annotation on one page write that page ONCE; (c) a created annotation is numbered past every number the file references (a page listing an undefined `40 0 R` over `/Size 6`: every new number > 40), Stage 3 guide I-12's rule for added objects. Planted controls P5 (the array never used) and P6 (no numbering floor) each failed the case written for it.

**A-25 · Desired state: an undo before a save writes nothing** ✅ *2026-10-05, T3*
**Steps** Add then undo, save; add, save, delete, save.
**Expect** The first save is refused `error.save.nothingToSave` (or writes no update); the second pair writes page 1's `/Annots` **back to its pre-annotation value** and no annotation object. **If it fails** S1.
**Result** Two highlights added and undone: the journal clean, the sink's delta EMPTY, the save refused `error.save.nothingToSave`, the file byte-unchanged -- and the model hash, which moved when they were added, back to its value. Added, saved, deleted, saved: the second update is **page 1's dictionary alone**, read back EQUAL to the dictionary before the annotation (`/Annots` removed, not `[]`: the page had none), no annotation object written; and undoing that delete after the save writes the page again listing **the same object** the first save created, found by its `/NM`. Beyond the Expect: on `annots-mixed.pdf` every annotation of page 1 deleted as ONE command, saved, undone and saved -- one object written each time, the listing restored entry for entry; on `annots-direct.pdf` a DIRECT annotation and one TWO pages list deleted and restored across a save, the model hash restored, both pages' listings entry for entry, and a later delete-and-undo writing nothing (the open-time names survive a save). Planted controls P2 (a listing put back never dropped), P9 (a removed direct entry not kept) and P12 (the hash blind to annotations) each failed it.

**A-26 · An edit preserves what it does not model** ✅ *whole 2026-10-20, T15 -- the redaction mark, the last kind; the rule 2026-10-05, T3; through the command 2026-10-05, T5 -- every kind's text, Highlight's colour; free text's redrawn appearance 2026-10-07, T7; the drawing kinds' 2026-10-08, T8; stamps' 2026-10-09, T10; a file attachment's 2026-10-12, T11; measurements' 2026-10-14, T12; each other kind's with its task*
**Goal** D6-5.
**Steps** For every annotation in `annots-unknown-keys.pdf` and `annots-mixed.pdf`: an `EditAnnotation` changing its colour, saved; the dictionary read back first-party.
**Expect** Every key the model does not own **equal in value** before and after — `/PieceInfo` and `/RawPageTestKey` included; `/RC` rewritten exactly when `/Contents` changed and untouched when it did not; `/AP` regenerated for the edited ones and **byte-identical** for annotations nobody edited. **If it fails** S1.
**Result so far -- the rule, not yet the command.** `EditAnnotationCommand` is T5's, so T3 holds D6-5 at `pdf/ops/AnnotationOps::editedDictionary`, the function that command will call: for **every** indirect annotation of `annots-unknown-keys.pdf` and `annots-mixed.pdf`, read back first-party -- nothing changed gives the dictionary unchanged; a new colour changes `/C` and every other key is EQUAL in value, `/PieceInfo` and `/RawPageTestKey` included; new text changes `/Contents`, rewrites `/RC` (escaped XHTML) exactly where the dictionary has one, and leaves `/AP` equal. **Not yet covered:** the save of an edited annotation, and `/AP` regenerated for an edited one (the case's Steps) -- T5. Planted P3 (the dictionary rebuilt from the model) failed it.
**Result at T5 -- through the command, saved and read back.** `tst_annotedit`, `[A-26]`: on `annots-unknown-keys.pdf` and `annots-mixed.pdf`, an `EditAnnotationCommand` giving NEW TEXT to every annotation whose text is not what it draws (all but free text) -- the update holds **exactly those dictionaries** (no page, no appearance), and read back from the saved bytes every key the model does not own is EQUAL in value, `/AP`, `/PieceInfo` and `/RawPageTestKey` included; `/Contents` is the new text, `/M` the command's instant, `/RC` rewritten exactly where the dictionary had one (escaped XHTML). A **recoloured highlight** from the file: the annotation and ONE new appearance, **redrawn by our builder** (its `/AP /N` a new stream that passes `ContentStreamBuilder::check`) and every other key equal. **A colour on a kind this build cannot draw yet** (a Square) is refused BY KEY -- `error.pdf.appearanceUnsupported`, context `Square`, the sink's own reason through `Command::refusal()` -- with nothing changed; the same Square MOVED is allowed, its producer's appearance carried with its rectangle (ISO 32000-1 §12.5.5). Planted P2 (a recolour keeping the old appearance) and P3 (the dictionary rebuilt from the model) each failed it. **Still owed:** "regenerated for the edited ones" for every kind but Highlight -- each kind's builder arrives with its tool (T6 …), and until then such a change is refused rather than drawn wrong.
**At T7 -- free text, whose appearance IS its text** (`tst_annotfreetext`, `[A-26]`): `annots-every-type.pdf`'s own text box given new text -- redrawn by our builder (a new `/AP /N`, decoded to the new text) and its `/DS`, which the model does not hold, kept; a box RECOLOURED -- `/C` and `/DA`'s colour both changed, `/DA`'s font kept, the text redrawn in the new colour. Planted P10 (the recolour leaving `/DA`'s colour) failed it.
**At T10 -- stamps** (`tst_stamps`, `[A-26]`): `annots-every-type.pdf`'s own `/Approved` stamp -- the corpus generator's, with its own appearance -- RECOLOURED: a NEW `/AP /N` of our builder's stroking in the new colour, every key but `/C`, `/M` and `/AP` EQUAL in value. A stamp RawPage cannot redraw (a foreign `/Name`, an image stamp reopened) is moved and resized with its producer's appearance carried, and a turn is refused by key (`error.edit.stampNotRedrawable`, D6-37(a)). *Moved by T10:* `tst_annotedit`'s *"a change of what a kind draws is refused by key when this build cannot draw that kind"* and `tst_annotationops`' builder refusal, which used the Stamp as the kind not yet drawn, use the FileAttachment (T11's).

**At T8 -- the drawing kinds, redrawn** (`tst_annotdrawing`, `[A-26]`): every Square, Circle, Line, Polygon, PolyLine, Ink and cloud of `annots-every-type.pdf` -- the corpus generator's, each with its own appearance -- RECOLOURED as one `SetAnnotationAppearanceCommand`: each with a NEW `/AP /N` of our builder's, passing the re-read and stroking in the new colour, and every other key EQUAL in value. A Line with leader lines and a caption (`/LL`, `/LLE`, `/Cap`) and a PolyLine with a `/Measure` -- built in the case -- are NOT redrawn: the model does not hold what they draw, so the change is refused BY KEY (`error.pdf.appearanceUnsupported`) and a move of either carries its producer's appearance (D6-35(j)). Planted P8 (such a line redrawn) failed it. The case that refused a recoloured Square until T7 refuses a Stamp now (T10's).
**At T11 -- a file attachment** (`tst_attachments`, `[A-26]`): `attachments.pdf`'s own pushpin MOVED -- not redrawn, its producer's appearance carried into the new rectangle -- and RECOLOURED: drawn anew by our builder (D6-38(i)), the colour read back from the saved file, and its `/FS` -- which the model holds as a description, never as bytes -- kept value for value, the file it carries the same SHA-256. The kind A-26's refusal case now uses is the SOUND (T14's), moved off the pushpin as T10 moved it off the stamp (`tst_annotedit`, `tst_annotationops`). Planted P18 (the pushpin not drawn) failed it.
**At T12 -- measurements, and what T8 refused** (`tst_annotdrawing`, `tst_measure`, `[A-26]`): T8's case is REWRITTEN, because the model now holds what it could not (D6-39(c)): the built Line with `/LL 15 /LLE 5 /Cap true` is read with its leaders and caption, RECOLOURED as one command and REDRAWN -- in the new colour, its line and its two leaders stroked (three `S` operators where T8 refused), `/LL` and `/LLE` read back -- while the built PolyLine whose `/Measure` states no number format is still REFUSED by key (`error.pdf.appearanceUnsupported`) and still carried by a move. `measure-scale.pdf`'s own three measurements recoloured are redrawn reading their labels, their producer's `/Measure` untouched; a producer's measurement this build cannot display (a 100-character unit, a zero factor) is moved, its move's line read back, and its recolour refused by key. Planted P18 (a line's caption not drawn) failed it.
**At T15 -- the redaction mark, the last kind Stage 6 writes** (`tst_redaction`, `[A-26]`): `annots-every-type.pdf`'s own mark -- the generator's, with its own outline and overlay -- RECOLOURED as one `SetAnnotationAppearanceCommand` and saved: every key the model does not own EQUAL in value, all but `/C`, `/M`, `/AP` and `/RO` (the two appearances D6-25 redraws), and no key added but those four. The sound stays the kind the refusal case uses (moved there at T11). Planted **P5** (an edit of a mark writing `/Repeat` whatever the model says) failed it. **So every kind Stage 6 writes has its A-26 case**, each with its task.

**A-27 · Everything RawPage writes has an identity** ✅ *whole 2026-10-20, T15 -- the pushpin, the redaction mark and the sound, the last kinds; Highlight 2026-10-05, T3; T6's kinds; free text 2026-10-07, T7; the drawing kinds 2026-10-08, T8; stamps 2026-10-09, T10; measurements 2026-10-14, T12 (each saved one read back by its `/NM`, `tst_measure`); each other type with its task*
**Steps** Create one of each type; save; read back.
**Expect** Each carries a `/NM` unique in the document, `/M`, `/CreationDate`, `/T` equal to `identity/author` (or the login name `app/` supplied), and `/P` naming its page. A direct dictionary, once edited, is indirect. **If it fails** S2.
**Result so far -- Highlight, the one type T3 writes.** Two highlights saved and read back first-party: each `/NM` 32 lowercase hex digits and the two distinct, `/T` the author passed, `/CreationDate` and `/M` the instant (`D:…Z`), `/F` 4 (Print), `/AP` present, `/P` page 1, colour, opacity, text and quadpoints as created; a non-ASCII author (`Zoë Müller 林`) written as UTF-16 and read back exactly. **`identity/author` and the login name are T6's** (D6-13): until then the author is the command's. Planted P13 (no `/NM` written) failed it -- through `verify`, which refuses a created annotation without its `/NM`.
**At T6 -- every kind T6 makes.** Underline, strikeout, squiggly, caret, note and pop-up saved and read back (`tst_annotmarkup`, and through the tools in `tst_markupink`'s A-34 case): each with its `/NM`, `/P` and dates -- the pop-up with no author and no dates of its own, as Acrobat writes it -- and `/T` **`identity/author`, else the login name `app/` supplied** (A-53). A note's `/F` is 28 (`Print`, `NoZoom`, `NoRotate`). *"A direct dictionary, once edited, is indirect"* is T5's (A-34's in-place case).
**At T7:** a box, a callout and a typewriter saved and read back (`tst_annotfreetext`, `[A-27]`), each with its `/NM`, `/P`, `/T` and dates, and through the tools by the person set (`tst_typedink`).
**At T10:** every standard stamp, a dynamic stamp, a JPEG and a pixel image stamp saved and read back (`tst_stamps`), each with its `/NM` (the stamp found by it), `/T`, `/P` and `/AP` -- verify refuses a created annotation without them.
**At T8:** a line, an arrow, a rectangle, an oval, a polygon, a polyline, a cloud of each intensity and an ink, saved as one command and read back (`tst_annotdrawing`, `[A-27]`): each with its `/NM`, `/M`, `/CreationDate`, `/T`, `/P` and `/AP`.
**At T15 -- the pushpin, the redaction mark and the sound** (T11's and T14's kinds, the last; `tst_redaction`, `[A-27][A-32]`): each made by its own constructor (`newFileAttachment`, `newTextRedaction`, `newSoundComment`), the three added as one command, saved and read back first-party -- each FOUND BY ITS `/NM`, an object of the file now, with `/NM`, `/M`, `/CreationDate`, `/P` and `/AP /N` written. **So every kind Stage 6 writes carries its identity**, each with its task's case.

**A-28 · Every command reverts exactly** ✅ *2026-10-05, T3, for the three commands `Commands.def` had; re-run at T5 for seven; at T6 and T7 with their shapes; at T9 for nine; at T10 with stamps; at T13 for twelve*
**Steps** `ctest -R reversibility` — each command's `[revert:Class]` case, and `tst_reversibility`'s sweep over every corpus file the command applies to.
**Expect** `apply` then `revert` restores the **structural hash** and the store's state; a refused `apply` leaves both untouched (partial application is a bug). **If it fails** S1.
**Result** `unit.reversibility of every registered command over every editable corpus file`: *"865 combinations over 100 editable corpus files; 0 failures"* -- `page.rotate`, `annot.add` (one highlight, and two on one page) and `annot.delete` (every annotation a page owns as one set, and the first alone, on the first and last page) -- model hash AND an empty sink delta after each revert; a refused `apply` leaves both untouched (`tst_annotationops`' refusal cases: a set holding one annotation the sink cannot write is refused WHOLE, nothing in the model, the history or the sink). Planted P1 (add's revert forgets the sink), P8 (delete removes front to back) and P10 (delete's revert forgets the model) each failed the sweep. **Re-run as each later command arrives.** **At T5, seven:** *"981 combinations over 100 editable corpus files; 0 failures"* -- the four new generators a MOVE of everything a page owns as one set and of its first alone, NEW TEXT on every annotation whose text is not drawn, a COLOUR and an opacity on the page's highlights, and every LOCK set and every lock cleared; `missingGenerators` names all seven rows. Each `revert` is refused unless the model is exactly what its `apply` left (`revertChange`). **At T6** (no command added; `annot.add`'s generator given T6's shapes): *"1309 combinations over 100 editable corpus files; 0 failures"* -- a NOTE AND ITS POP-UP as one set, naming each other, and a REPLACE: a caret with a strikeout, an underline and a squiggly in its group.
**At T7:** *"1473 combinations over 100 editable corpus files; 0 failures"* (1309 at T6) -- `annot.add` gaining a box, a callout and a typewriter, Latin and CJK, as one set, and `annot.edit` now giving free text new text too, redrawn (each free text this build can draw). T7 adds no command; `mutation-lint` still names seven.
**At T10:** *"1910 combinations over 100 editable corpus files; 0 failures"* (1745 at T9) -- `annot.add` gaining a standard stamp, a dynamic one, one turned 30 degrees and the same image twice as one set, and `annot.setAppearance` a recolour and an opacity on every stamp a page owns that RawPage can redraw (the corpus's `/Approved` among them); `annot.edit`'s new text now redraws a dynamic stamp (D6-37(d)). T10 adds no command; `mutation-lint` still names nine.
**At T9, nine:** *"1745 combinations over 100 editable corpus files; 0 failures"* (1650 at T8) -- the two new generators a REPLY to the first comment a page owns and to each of its first two as one set, and a REVIEW STATUS on the first and a CHECKMARK on every one as one set, each answering only what may be answered (not a pop-up, a state reply, a `ReadOnly` one or a direct dictionary -- the sweep's first run found that last refused with no reason, I-14); `missingGenerators` names all nine rows.
**At T13, twelve:** *"2300 combinations over 100 editable corpus files; 0 failures"* -- the new generator an IMPORT that only adds (a highlight and a note with its pop-up, naming each other by name, as an exchange brings them) and one PLANNED BY THE MERGE as the shell plans one (the page's own comments exported, brought back newer with new words, every one this build can write replaced in place); `missingGenerators` names all twelve rows. Planted P9 (the import's undo forgetting what it replaced) failed it.
**At T8:** *"1650 combinations over 100 editable corpus files; 0 failures"* -- `annot.add` gaining the drawing kinds as one set (a line, an arrow, a dashed rectangle, a filled oval under multiply, a polygon, a polyline with heads, a cloud, an ink of two strokes), and `annot.setAppearance` a dashed, wider, multiplied outline in a new colour on every drawing kind a page owns that this build draws. T8 adds no command.

**A-29 · Every command serializes, and recovery replays it** ✅ *2026-10-05, T3, for the three commands `Commands.def` had; T5 for the four in-place ones; T9 for the two that answer one; T10 with stamps; T13 for the import*
**Steps** Each `[serialize:Class]` case; a journal of one of each command replayed by `RecoveryJournal` against the file as opened.
**Expect** `serialize → deserialize → apply` reaches the same desired state; replay reaches it too; no command's line names an object number a compose allocated (D6-4). **If it fails** S1.
**Result** `[serialize:AddAnnotationCommand]` and `[serialize:DeleteAnnotationCommand]` round-trip 3 + 2 samples (every identity kind, a set across pages, the page-index limit, non-ASCII text); **30 hostile inputs** each refused `error.command.malformed` with a bounded context (a 10 MB `/NM`, an object id where a created one belongs, a colour of five, opacity 2, 200,008 coordinates, 100,001 ids, …) and the boundaries accepted. A journal of add, delete (an annotation the file holds AND one created in the same session), add and undo, recorded by `RecoveryJournal` and replayed into a fresh open of the same file: the **same model hash**, redo available, and a composed update **byte-identical** to the original session's (SHA-256) -- and no journal line names an object number the compose allocated (D6-4).
**At T5:** `[serialize:EditAnnotationCommand]`, `[serialize:MoveAnnotationCommand]`, `[serialize:SetAnnotationAppearanceCommand]` and `[serialize:LockAnnotationCommand]` round-trip every identity kind through `CommandRegistry` (and `tst_command_serialize`'s table, now 29 samples), and refuse by key **30 hostile inputs** -- another command's type, a 10 MB `/NM` or id, an offset past every page, no move at all, an instant before 1970, past 9999 or fractional, a colour of two or five components or a component past one, an opacity past one, a negative or a 10^9 border, a property this build does not know, `Hidden` as a lock, one bit both set and cleared. A journal of a move, a recolour, new text and two locks with an undo, replayed into a fresh open of the same file: the **same model hash** and a composed update **byte-identical** (SHA-256).
**At T8** (`tst_annotdrawing`, `[A-29]`): an `AddAnnotationCommand` of every drawing kind round-trips with its border style, dash, cloud and blend, and `SetAnnotationAppearanceCommand` with a line style, a blend, two endings, one ending (a callout's) and a cloud's intensity; refused by key: a border style ISO does not name, a negative dash, a dash of nothing, seventeen dashes, a cloud past 2 or below 0, a blend ISO does not name (and, on the bar's command, one no tool offers), no ending, three, an ending ISO does not name, a line style there is not or that is a number.
**At T10** (`tst_stamps`, `[A-29]`): an `AddAnnotationCommand` of an image stamp round-trips -- its pixels with it, base64, **inside its journal line** until T11's side files (D6-23) -- and four hostile shapes are refused `error.command.malformed`: pixels on a Square, a JPEG whose header disagrees with its width, bytes that are not base64, an encoding no writer has. A journal of a standard and a dynamic stamp, a JPEG and a pixel image stamp, a turn and an undo, recorded by `RecoveryJournal` and replayed into a fresh open of `text-a4.pdf`: the **same model hash**, the turn redoable, and the composed update **byte-identical** (SHA-256). Planted P16 (pixels accepted on any kind) failed it.
**At T9** (`tst_command_serialize`, `tst_annotthreads`): `[serialize:ReplyToAnnotationCommand]` and `[serialize:SetAnnotationStatusCommand]` round-trip 2 + 3 samples (a reply to an object and to a created reply; statuses in both models, as sets), and **thirteen hostile shapes** are refused by `Result` -- a reply that is a highlight, has no parent, is in a group, carries a state, names a direct parent or an object id; a status with no model, an unknown model, a state its model does not allow, an unknown state, not hidden, no parent, a square -- and a reply handed to the status command, or a status to the reply command, is refused too. A journal of two replies (the second to the first, a CREATED parent), a status, two checkmarks as one set and a status undone, recorded by `RecoveryJournal` and replayed into a fresh open of the same file: the **same model hash**, the status and the nesting as made, and a composed update **byte-identical** (SHA-256). Planted P17 (a status serialized one annotation short) failed it.
**At T13** (`tst_command_serialize`, `tst_fdf`): `[serialize:ImportAnnotationsCommand]` round-trips 3 samples (an import that adds a note and its pop-up naming each other, one that only replaces an object's annotation, one that does both with an Acrobat-shaped stamp) and **twelve hostile shapes** are refused by `Result` -- no `added`, no `replaced`, both empty, `added` an object, an annotation a number, an added annotation with an object id or whose `/NM` is not its id, one name added twice, a replaced one with no rectangle or an opacity of 2, one identity replaced twice, past the count bound. `unit.recovery replays imports to the same model and the same bytes`: a journal of three imports -- every kind `every-type.xfdf` holds, added; a comment of the document replaced beside one added; a third, undone -- recorded by `RecoveryJournal` and replayed into a fresh open of `annots-threads.pdf`: the **same model hash**, the third redoable, and the composed update **byte-identical** (SHA-256). Planted P10 (a journal line adding one name twice accepted) failed it.
**At T6:** a reference to another annotation serializes as an object number or a CREATED identity (`nm:<nm>`) and nothing else -- a note and its pop-up round-trip through `AddAnnotationCommand` equal; a `direct:` identity and a 10 MB name in a reference are refused `error.command.malformed` (`tst_annotmarkup`, `[A-29]`).

**A-30 · Save verifies annotations too** ✅ *2026-10-05, T3*
**Steps** `RevisionWriter::verify` over a correct update; then with `setComposeFaultForTesting` deleting the new annotation from the composed bytes.
**Expect** The correct one passes; the faulted save **fails by key before any replace**, the temporary file removed, the target byte-unchanged. **If it fails** S1.
**Result** With `setComposeFaultForTesting` renaming the update's `/Annots` (every Stage 3 check -- prefix, parse, pages, rotations -- still passes): the save fails `error.save.verifyFailed`, context `annotations:1`, **before any replace** -- no `replaceFile` call, no temporary file left, the target byte-unchanged, the journal still dirty; the same save without the fault is written (the control). A second fault -- the appearance's `f` turned into an operator Annex A does not have -- is refused the same way, by `ContentStreamBuilder::check` inside verify. **This mechanism found T3's own first defect before any case asserted it:** the appearance was built as a plain dictionary, which the writer writes with its data DROPPED -- every save failed verify on page 1. Planted P4 (verify skips the annotations) failed it. **At T5, edits too:** `verify` reads every EDITED annotation back -- the dictionary its listing names has its `/Subtype`, its new `/Rect` and, where it was redrawn, a well-formed `/AP` of our own; a moved rectangle put back in the composed bytes fails the save `error.save.verifyFailed`, `annotations:N`, before any replace, the target byte-unchanged, and the control is written (`tst_annotedit`, `[A-30]`). Planted P14 (verify blind to an edit) failed it.

**A-31 · `qpdf` finds nothing wrong with what we write** ✅ *(Q8) -- the step written 2026-10-05, T3; its log read 2026-10-08 from CI run 37655927348's `portable-asan` job (and 37653328339's): "qpdf: qpdf version 11.9.0", every file "No syntax or stream encoding errors found", "qpdf check: 44 file(s), 0 failed; the truncated-stream control rejected" -- the headless suite 860/860 under ASan+UBSan with no sanitizer report*
**Steps** Every file the suite saves is copied into `<build>/written/`, and the `portable-asan` job -- which runs that suite -- passes them to `qpdf --check` through `tests/gates/check-qpdf.py`; read the **log**. *(Corrected at T3: written first as the `gates` job's, which runs no suite.)*
**Expect** The qpdf version printed; *"No syntax or stream encoding errors found"* for every file; the file count printed and non-zero. **The control:** a file with a truncated stream fails. **If it fails** S2.
**Result so far** `tst_annotationops` copies every file it saves -- **15** on Windows -- into `<build>/written/`; `ci.yml`'s **`portable-asan`** job installs `qpdf` and runs `tests/gates/check-qpdf.py` over that directory after the suite: the version printed, a copy of the first file with five bytes cut from inside its last stream judged FIRST and required to fail, then every file required to exit 0 with *"No syntax or stream encoding errors found"*, and an empty directory refused. **Deviation from P-4:** the plan named the `gates` job, which builds no Qt and runs no suite -- it has no file to judge. The script's logic was exercised here against a fake `qpdf`: an honest fake had its control rejected and every file passed; a fake that accepts everything stopped the run at the control; no files failed it. **Owed:** the job's log, from the first CI run after this is pushed (rule 3) -- this machine has no `qpdf`.

**A-32 · A full round trip keeps every annotation** ✅ *whole 2026-10-20, T15 -- the pushpin, the redaction mark and the sound, the last kinds; Highlight's half 2026-10-05, T3; T6's kinds; free text 2026-10-07, T7; the drawing kinds 2026-10-08, T8; stamps 2026-10-09, T10; measurements 2026-10-14, T12 (their `/Measure`, leaders and readings, A-88)*
**Steps** Open each annotation fixture; add one annotation of every type through commands; save; reopen in a fresh process.
**Expect** The store read back equals the store before the save in every modelled field, the originals included. **If it fails** S1.
**At T6:** a note and its pop-up, and a replace's caret and strikeout, saved and read back first-party with every reference resolved to the object its target was written at; a saved note MOVED later still naming its pop-up's object; and a reference whose target was deleted before the save left OUT rather than written pointing at nothing (`tst_annotmarkup`).
**Result so far** Only Highlight is written at T3, so "one annotation of every type" waits for the types. What holds now: a highlight added to `text-a4.pdf`, `annots-mixed.pdf`, `objstm-xrefstream.pdf`, `annots-direct.pdf` and `annots-every-type.pdf`, saved, and reopened FRESH by the shipped backend -- every page's `/Annots` read alike by the engine and the first-party reader (D6-3, over files our writer wrote), the highlight in the store by its `/NM` on its page (`render.the engine reads a highlight our writer saved as the first-party reader does`); and A-27's field-by-field read-back.
**At T10:** every standard stamp read back with its `/Name`, `/C` and rectangle; a dynamic stamp with its baked `/Contents`; an image stamp with `/IT /StampImage`, no `/Name` and its `/Subj` (`tst_stamps`, `[A-75][A-76][A-77]`) -- and, as D6-37(a) says, WITHOUT its pixels: a reader holds none, so the image stamp read back is foreign, its appearance carried.
**At T8:** every drawing kind saved and read back first-party EQUAL to what was made in every modelled field -- numbers within a written real -- `/L`, `/LE`, `/Vertices`, `/InkList`, `/BS /W /S /D`, `/BE /I`, `/IT`, `/IC`, `/CA`, `/BM` among them (`tst_annotdrawing`, `[A-32]`); planted P6 (`/BS` losing its dash) failed it.
**At T7:** the three typed kinds saved and read back first-party with every field they set -- the rectangle within a written real, `/DA`, `/IT`, `/CL`, `/LE`, `/RD`, `/BS /W` -- and `/Q`, which the model gained at T7: a centred box read back centred, its line set where a centred line goes (`tst_annotfreetext`, `[A-32]`); planted P11 (`/Q` not written) failed it. The reader's case compares `/Q` with the labels too (`tst_annotreader`), and a journal line carries it (`justification`, 0 to 2, else malformed).
**At T15 -- the pushpin, the redaction mark and the sound** (`tst_redaction`, `[A-32]`, the case above): each read back EQUAL to what was made in every modelled field -- the whole `Annotation::toJson`, less the identity (`Created` before, `Object` after) and the payload's bytes, numbers to three decimals -- and the payloads by SHA-256 from a document opened on the saved file: the pushpin's file and the sound's samples exact. Planted **P6** (the reader dropping a mark's `/OverlayText`) failed it. **So every kind Stage 6 writes has its round trip**, each with its task's case; the Steps' *one of every type over each fixture in a fresh process* is that set, not one case.

**A-33 · A4: a comment through the product, judged by OpenSSL** ✅ *2026-10-05, T3*
**Goal** §3.4's automated half.
**Steps** `ctest -R a4_product -V` extended: each `corpus/signed/` file opened through the composition, one note added by `AddAnnotationCommand`, saved through `SaveCoordinator`; `tests/gates/check-signatures.py` over the outputs, its flipped-byte controls first.
**Expect** **8 of 8 controls rejected, then 4 of 4 verified**; each original the byte-identical prefix of its output. Outputs left in `build\x64-release\a4-product\` for M-03. **If it fails** S1.
**Result** `render.a4_product.annotation.openssl` (`-R a4_product` runs it beside Stage 3's rotation case, which is unchanged): each `corpus/signed/` file opened by the shipped backend, a HIGHLIGHT added by `AddAnnotationCommand` (the first annotation command; a note's appearance is a later task's), saved by `SaveCoordinator` and the real `WinDurableFiles`; each original the prefix of its output read back from disk, and each output reopened fresh with both readers agreeing on every page. OpenSSL 3.5.6: *"all 8 flipped copies rejected"*, then *"signature check ok: 8 of 8 controls rejected, 4 of 4 signatures verify"*. **Deviation:** the outputs are in `build\x64-release\a4-product-annot\`, not `a4-product\`, where Stage 3's rotated outputs keep their names -- M-03 walks the new folder.

### 5.6 How it reaches the pixels — A2 at Stage 6 (T4)

**A-34 · What is shown is what is saved** ✅ *whole 2026-10-20, T15 -- A-108's sweep over what no suite had compared; Highlight through commands 2026-10-05, T4; T6's kinds through their tools; free text through its tools 2026-10-07, T7; the drawing kinds 2026-10-08, T8; stamps through the stamp tool 2026-10-09, T10; measurements through the measuring tools 2026-10-14, T12 (`tst_measureproduct`); each other type through its tool with its task*
**Goal** §3.2.
**Steps** `tst_annotink` (composition): one annotation of every type created **through its tool**, the live page rasterized; saved; reopened fresh; rasterized at 72 and 144 dpi and at a 90° view rotation.
**Expect** **0 differing pixels** in every comparison. **The control:** a reload skipped (the live engine left on the old bytes) makes every comparison differ. **If it fails** S1.
**Result so far -- the one type the sink writes, created by constructing its command (no tool exists before T5).** `tests/render/tst_annotdisplay.cpp`, over the shipped backend and the journal's show step set as `DocumentWindow` sets it: a highlight on nine pages -- `text-a4.pdf`, `annots-mixed.pdf` (nine annotations), `objstm-xrefstream.pdf` (its `/Annots` in an object stream), all four of `annots-rotated.pdf`'s rotations, `annots-direct.pdf` and page 450 of `text-900.pdf` -- rasterized live and from the saved file reopened fresh, at 72 and 144 dpi and turned 90°: **0 differing bytes in every comparison**, and the live page changed by the edit in every one (rule 1). A rotation and a highlight on one page, in both orders, on a page at `/Rotate` 0 and one at 90: **0** -- the rasterizer's correction for a reloaded `/Rotate` (D6-2). Through the shipped SCHEDULER and cache (the named work of A-38, below): every 256-pixel tile of an edited page at 2 px/pt, under view rotations 0, 90 and 180, on pages at `/Rotate` 90 and 270 and with an unsaved rotation as each tile's delta -- 35 tiles, **4 to 8 drawn again and the rest served from the cache at the new revision** -- each equal to the same tile of the saved file, **0 differing bytes**. Headless, under `portable-asan` too: the update `showEdits` hands the engine is byte for byte the update the save appends (`tst_annotationops`). **The control:** the show step left out -- the live page unchanged by the edit and different from the saved file in all three views. **Owed:** every other type, through its tool, in `tst_annotink`.
**At T6, THROUGH THE TOOLS** (`tst_markupink`, `[A-34][A-35]`): on `text-a4.pdf`, seven gestures with mouse and key events -- a highlight, an underline, a strikeout and a squiggly each dragged across a line, a replace dragged and typed, an insertion clicked and typed, a note clicked in the margin and typed -- seven commands; saved with *Save*, the file reopened in a FRESH engine document: *"live against the file reopened fresh: 0 differing bytes over three views"* (72 and 144 dpi, turned 90°), and each view changed by the edits (rule 1). **Still owed:** every kind T8 … T14 makes.
**At T7, THE TYPED TOOLS** (`tst_typedink`, `[A-34][A-35]`): a box dragged, a callout pressed and dragged, a typewriter clicked -- each typed and finished with `Enter` -- and the box edited in place: four commands; saved with *Save* and reopened fresh: *"live against the file reopened fresh: 0 differing bytes over three views"*, each view changed by the edits; then all four undone after the save: *"0 bytes from the original"*. And typed on `annots-rotated.pdf`'s page at `/Rotate` 90: written `/Rotate` 90 and reading LEFT TO RIGHT across the screen -- planted P15 (written upright on the page, not as shown) failed it, and P7 (the frame turned the wrong way) did not until the case asked which way the line reads as well as that it runs across (the T7 row).
**At T10, THE STAMP TOOL** (`tests/composition/tst_stampink`, `[A-34][A-35]`): on `text-a4.pdf`, `Ctrl+Alt+S`'s command arms the tool and shows the palette; `NotApproved` and `DynamicReviewed` chosen in the palette and CLICKED, the corpus PNG clicked and the corpus JPEG DRAGGED out (fitted into the box dragged, 240 by 90 points, its proportions kept) -- four gestures, four commands, the dynamic stamp's lines starting with the composition's login name; saved with *Save* and reopened fresh: *"A-34 through the stamp tool: four stamps saved, live against the file reopened fresh: 0 differing bytes over three views"* (72 and 144 dpi, and turned 90), each view changed by the stamps; all four undone after the save: *"0 bytes from the original"* (A-35).
**At T8, THE DRAWING TOOLS** (`tst_drawink`, `[A-34][A-35]`): on `text-a4.pdf`, a rectangle, an oval and an arrow DRAGGED, a polygon clicked and finished with `Enter`, a polyline finished with a double click, a cloud, and a pencil stroke of forty moves -- seven gestures, seven commands; saved with *Save* and reopened fresh: *"live against the file reopened fresh: 0 differing bytes over three views"*, each view changed by the edits; then all seven undone after the save: *"0 bytes from the original"*. Planted P20 (the polyline tool writing a Polygon) failed it; its first plant did not compile and was replaced (the T8 row).
**At T5, edits in place** (`tst_annotdisplay`, `[A-34][A-35]`): every annotation of `annots-every-type.pdf` page 1 MOVED as one command (every kind, each with its producer's appearance carried), `annots-direct.pdf` page 1 (a direct dictionary promoted to an object), `annots-rotated.pdf` page 2 at `/Rotate` 90, a highlight from `annots-mixed.pdf` RECOLOURED (redrawn by our builder), and a Square's new text and a lock (drawn the same): the live page **changed by each visible edit and unchanged by the two invisible ones**, and live against the saved file reopened fresh **0 differing bytes** at 72 and 144 dpi and turned 90°. **Still owed:** creation through each TOOL, T6 … T14.
**At T15, THE SWEEP** (A-108, `tests/composition/tst_shownsaved`): an ERASE, a PASTE, the PUSHPIN and a SOUND -- the last kinds and gestures no suite above compared in pixels -- made with their tools beside a pencil stroke on `text-a4.pdf` and saved together: *"0 differing bytes over three views"*, live against the file reopened fresh, and all five undone after the save 0 bytes from the original. **So every kind and gesture Stage 6 makes is compared through its tool**: T6's in `tst_markupink`, T7's in `tst_typedink`, T8's in `tst_drawink`, T10's in `tst_stampink`, T12's in `tst_measureproduct`, T13's import in `tst_exchangeproduct`, T14's marks in `tst_redactink`, and the rest here.

**A-35 · An undo shows the page as it was** ✅ *2026-10-05, T4, for Highlight; re-run as each type arrives -- T6's kinds, T7's typed text, T8's drawing kinds and T10's stamps through their tools (A-34's T6, T7, T8 and T10 paragraphs)*
**Steps** Each type added and undone; the page rasterized.
**Expect** **0 pixels** from the original file's raster. **If it fails** S1.
**Result** On `text-a4.pdf`, `annots-mixed.pdf` and `annots-rotated.pdf` page 3 (`/Rotate` 180): added, undone -- **0 differing bytes** from the page before the edit at 72 and 144 dpi and turned 90° -- and redone, **0** from the page as first edited. Headless: an undo is shown too, and an undo back to the file on disk shows no update at all. **At T5, the in-place edits above, undone AFTER their save: 0 differing bytes from the original file's page in all three views** -- which the first run did NOT give for the recoloured highlight (**I-08**, S1): the undo redrew it with our builder in its old colour instead of restoring the producer's appearance. Since its fix an undo that returns to a version the file held writes that dictionary verbatim (`[I-08]` in `tst_annotedit`: the restored dictionary equal to the original, `/AP` reference included, and the update the annotation alone). Planted P12 (redraw rather than restore) failed it. **At T6, through the tools:** all seven of A-34's gestures undone AFTER the save -- *"0 bytes from the original"* in all three views.

**A-36 · Only the scope is invalidated** ✅ *2026-10-05, T4; its search clause 2026-10-08, T9 (D6-11)*
**Goal** D6-10.
**Steps** On `text-900.pdf`, search once (the index complete), render pages 449–451, add a highlight on 450.
**Expect** Pages 449 and 451 served **from the cache** after the edit (hit counts); page 450 re-rendered; **no text job** for page 450 (the index keeps its text); a search for the highlight's comment text finds it (D6-11). **The control:** an annotation command whose scope says *content* re-extracts page 450. **If it fails** S2.
**Result** Through the shipped scheduler, cache, `DocumentIndex` and text extractor on `text-900.pdf`, its pages 449-451 extracted and drawn: after a highlight on 450, **2 tiles from the cache, 1 drawn again -- with the highlight in it -- and no text job**, page 450's text still held; **the control**, a CONTENT scope on page 450, drops its text. Headless: `Scope::annotationsOf` keeps text and merges outwards to content; the index keeps an annotated page's text, drops its search units, tells subscribers an EVICTION (the text is what it was, so a selection or a reflow keeps it), and the next comment search reads the page's units again and extracts no text. **Beyond the Expect -- the named work of A-38:** an annotation command's scope now names each changed annotation's RECTANGLE, padded a point, and the whole page for a popup, an annotation with one, `NoZoom`, `NoRotate` or a rectangle that is not a box; the cache and the scheduler drop only the tiles a rectangle reaches (`render::tileTouchedBy`, placing it through the crop box, the engine's `/Rotate`, the delta, the view rotation and the scale -- every rotation checked by hand values), and a queued, running or finished tile of the document the mutation does not reach is RE-KEYED to the new revision instead of cancelled (**I-06**). **The search clause, at T9** (`tests/composition/tst_commentscale`, `[A-36][A-74]`): a highlight carrying the comment *Quokkas, not marmosets*, made and NOT saved, is found by the product's find bar with `search/includeComments` on -- one hit, its unit's locator the highlight's identity, and the highlight SELECTED -- because a run of an open document is handed the store's comments (`SearchEngine::commentUnits`) and takes none from the engine's page parts. The index's half is above and unchanged: an annotation's scope still keeps the page's text. Planted P9 (the store's comments not used) failed it.

**A-37 · The reload happens where and when it must** ✅ *2026-10-05, T4*
**Steps** `tst_annotdisplay`: the reload under a burst of commands while tiles are in flight; thread ids and the scheduler's state recorded.
**Expect** Every `FPDF_LoadCustomDocument` on the **render thread**, never the UI thread; each swap **between jobs** — no tile in flight against a document the swap closed; **no copy of the original bytes** — the process's live heap grows by less than 10 % of `pages-900.pdf`'s size per reload. **If it fails** S1 for a job against a closed document; S2 otherwise.
**Result** Twenty highlights on page 450 of `pages-900.pdf` while three pages' tiles were queued around each: the swaps were taken by jobs on the WORKER, never the thread driving the journal (the run printed *"reloads 15, coalesced 5, failures 0"* -- a burst coalesces); the newest revision shown; **no failed job and no engine entry without the lock** (`unlockedEngineEntries() == 0`) -- the swap is made under the engine lock at the top of a job, so no job can hold a page of the document it closes; the engine reads the original from **the very buffer the backend holds** (the data pointers equal); and the live heap over ten add-and-undo pairs grew **78 bytes a reload** against a bound of 12,645. **Controls:** the original deep-copied per reload fails the pointer check; the reload taken on the calling thread fails the thread check.

**A-38 · A2 at Stage 6, graded** ✅ *2026-10-05, T4 -- the verdict recorded; T15 re-reads it (A-104)*
**Goal** §3.1, the bench half.
**Steps** `rawpage_bench_pipeline.exe --stage6 > bench-stage6.txt`, three runs; read the file.
**Expect** The bench **prints the dpi, the viewport and the file it reached** (Stage 4's A-72); per file: viewport p95, edit-to-pixels p95 and maximum, compose, reload and path-geometry distributions. Graded on §3.1 and **recorded** in `Tech-Stack-Plan.md` §3.3 as *A2 at Stage 6*. **If it fails** — a no-go is a verdict, not a defect: D6-2 changes before T5.
**Result -- VERDICT `go, with named custom work`, the user's choice on 2026-10-05; the named work built in T4; NOT inside §3.1 after it, and recorded as such.** `rawpage_bench_pipeline --stage6` (phase 7), each 30-second run printing the dpi (288), the viewport (1920x1080), the files and the page it edited. **Deviation:** each edit is a HIGHLIGHT of 50 quadrilaterals (400 coordinates, a 200-sample ink stroke's count) in a fresh cell, because the sink writes Highlight alone until T8; the path-geometry time is not reported (`pageGeometry` is T12's). **The first measurement**, before the named work, two runs: the viewport's p95 **112.5-114.7 ms** on the 900-page files (the viewer alone 9.1-13.2), edit-to-pixels p95 **14.7-15.4 s** -- every file outside. The leg breakdown named the cause: the reload itself costs **p95 1.9-2.0 ms** and the pump was never late (p95 0.02 ms) -- **the render thread spent 23 of 30 s redrawing the edited page's 40 viewport tiles** after every edit, each heavier as highlights accumulated, and every revision cancelled every queued tile of the document (9,051 in a run, guide **I-06**). The mirror the plan names for a no-go would have shared all of that, so the user chose `go, with named custom work` and the work in T4: **an annotation edit drops only the tiles its rectangle reaches, and a revision re-keys every tile it does not reach** (D6-10 extended; A-34, A-36). **After it, three runs** (run 3's viewer-alone column itself read 11.9 / 17.7 ms against 9.2-9.3 / 13.1-13.3, a slower machine that hour): the edited page's redrawn tiles **528-586 a run against ~7,400**, its engine time 7.1-10.4 s against 23; viewport p95 **pages-900 49.6 / 96.4 / 105.7 ms, text-900 99.5 / 99.6 / 107.6, objstm-xrefstream 0.05 / 0.56 / 53.4**; edit-to-pixels p95 **pages-900 47 / 49 / 301 ms, text-900 109 / 154 / 301, objstm-xrefstream 43 / 246 / 243**, max **301-360 ms**; the reload p95 2.4-3.8 ms; compose and verify on the UI thread p95 45-67 ms (verify 17-30); 300 reloads a run, none failed. **Outside §3.1 on the viewport everywhere but the three-page file's first two runs, and on edit-to-pixels in six of nine readings and every maximum.** **What remains, named, for T15's re-reading (A-104):** (1) **a tile of a page carrying hundreds of annotations costs p95 38-50 ms, max 40-63 ms, on its own** -- PDFium draws every annotation of the page in every tile -- §3.1's *"a page whose re-render alone exceeds the budget"*; (2) **compose and verify on the UI thread grow with the unsaved annotations** (`journal.run` p95 47-68 ms by 300), which now shows in the viewport as the pump's leg (p95 up to 40 ms, max 54) -- named at T4 and not built: each created annotation's objects serialized once rather than on every compose. Recorded in `Tech-Stack-Plan.md` §3.3 as *A2 at Stage 6*.

**A-39 · A reload that fails leaves the page drawing** ✅ *all but the status bar 2026-10-05, T4; the status bar 2026-10-05, T5*
**Steps** A compose fault (as A-30) during an edit.
**Expect** The engine keeps the previous document and **keeps drawing it**; the edit is refused by key, the command not left in the journal; the status bar says so. **If it fails** S1.
**Result so far** A-30's fault -- the update's `/Annots` renamed -- during an edit: refused `error.edit.notShown` (both catalogs), context `error.save.verifyFailed:…`, because the journal's show step composes and VERIFIES as Save does, so an edit Save could not write is refused when it is made; nothing undoable, not dirty, the model hash and the sink's delta as before, nothing queued for the engine, and the page drawn exactly as before (**0 differing bytes**); the control, unfaulted, is made and shown. An undo or redo the show step refuses is taken back and answers false. Below the first-party check, bytes the ENGINE refuses: the swap does not happen, the failure is counted and logged, the page drawn as before, and the document put back on its own bytes is no work. **At T5, the status bar:** `tests/ui/tst_annotoverlay`, `[A-39]` -- a drag on a rectangle with the show step refusing (`FakeBackend::failShows`): the annotation where it was, no step in the history, and the status bar saying why **in words** (`shell.status.annotationRefused` over the reason's own text -- never a key); the control, unrefused, moves it. A lock's refusal reaches the status bar by the same path (A-43).

### 5.7 The overlay, the context chain, the properties bar (T5)

**A-40 · Every chord can be pressed from where a person is** ✅ *2026-10-05, T5* *(Q1) -- the stamp chord's palette 2026-10-09, T10*
**Steps** `tst_annottools`, from key events: with the page focused and **nothing armed**, each of the fourteen `Ctrl+Alt+` chords; with a tool armed, `Ctrl+=`, `Page Down` and `Ctrl+0`; `tst_keybindings` against `Features.md` §8.
**Expect** Each chord arms its tool (or opens the stamp palette, or arms the redaction mark); with a tool armed the Viewer chords still act (the chain); `tst_keybindings` agrees with §8 row for row, its census at **thirteen** Write-mode collisions, 9 + 4 + 0 — `Ctrl+Alt+M` the ninth over Viewer (Q1's consequence, accepted 2026-10-04) — and `Ctrl+Alt+M` still a margin comment in Write mode. **If it fails** S2.
**Result** ✅ *2026-10-05, T5.* `tests/ui/tst_annottools`, from KEY EVENTS through the application (the window's `ShortcutBinder` sees each first): with the page focused and nothing armed, **each of the fourteen `Ctrl+Alt+` chords arms its tool** -- the stamp chord and the redaction chord arm the Stamp and the RedactMark tools, the palette being T10's -- puts the page in Annotation tools and says so in the status bar; with the highlighter armed, `Ctrl+=` zooms, `Ctrl+0` sets a fit, `Page Down` pages, the tool stays armed, another arming chord SWAPS the tool, `K` toggles keep-the-tool, `Esc` returns to the Selection tool and the Viewer, where `K` is nothing. `tst_keybindings` (`[D6-9]`): the chain resolves zoom, paging and arming through the Viewer, Annotation tools' own `Ctrl+E`, `Delete` and `K` win, the Viewer's arrows stay widget-native, Common and Global still reached -- and NO other context has a chain; its §8 row-for-row cases and the census at thirteen unchanged and green. Planted P1 (the chain removed) failed it. **What each tool creates is its task's (T6 …)**: armed at T5 a tool selects, moves and edits what is there and creates nothing.
**At T10, the stamp chord** (`tests/ui/tst_stamps`, `[A-40]`, a KEY EVENT through the application): `Ctrl+Alt+S` from the page with nothing armed arms the stamp tool AND shows the stamp palette, which T5 had left to T10. Planted P13 (the palette not shown) failed it.

**A-41 · An annotation is hit where it is drawn** ✅ *2026-10-05, T5*
**Goal** Rule 6.
**Steps** `tst_annotink`: for each annotation in `annots-every-type.pdf` and `annots-rotated.pdf`, a click at a point inside its **rendered ink** (found in the raster) and one 3 device px outside its ink; at device scale 1.0 and 1.5 at an odd logical offset (`CLAUDE.md`'s 150 % trap).
**Expect** Inside selects it, outside does not; `NoZoom` and `NoRotate` annotations hit where they are drawn. **If it fails** S2.
**Result** ✅ *2026-10-05, T5.* `tests/composition/tst_annotink`, `[A-41]`, over the product's composition and the shipped engine: each annotation's INK is the page drawn with annotations minus the page drawn without, both asked for by the case through the scheduler; at **two device scales, 1.39 and 2.08 pixels a point** (zoom 1.0 and 1.5 at offscreen's 100 dpi), the view scrolled by an odd 3 and 7 pixels, over `annots-every-type.pdf` and all four pages of `annots-rotated.pdf`: *"72 annotations graded, 72 inside and 72 outside points, 13 of them beside a stroke inside its rectangle"* -- each inside point (the ink pixel nearest the ink's centroid with ink on all four sides) selects its annotation, each point with **no ink within three whole pixels** does not. A box kind and text markup are graded past their box as well (D6-30); a SHAPE's outside point lies inside its rectangle, where a rectangle hit test fails. **The first run failed**, and the failures were the product's (**I-07**): a stroke's reach taken from the rectangle's margin hit a plain polygon, a polyline and a line three pixels off at 2.08 px/pt, and the `NoRotate` notes on the 180° and 270° pages missed -- PDFium draws them **upright at the rectangle's upper-left corner** (measured by a throwaway probe at every quarter turn and both zooms; the icon 27 px, then 40, so **PDFium 153 does not honour `NoZoom`**). Planted P9 (every kind hit over its rectangle; in `tst_annotedit`'s hand shapes) and P10 (a `NoRotate` note hit at its rectangle) each failed. **What it does not cover:** a real 150 % monitor -- offscreen draws at a device-pixel ratio of 1, so the 1.5 here is the zoom's; M-01 walks both monitors.

**A-42 · One gesture, one command** ✅ *2026-10-05, T5*
**Steps** Drag-move, drag-resize, `Shift`+drag (constrained), `Alt`+drag (duplicate), and a drag abandoned with `Esc`.
**Expect** Each completed gesture is **one** `MoveAnnotation`, `EditAnnotation` or `AddAnnotation` in the journal; the constraint holds to 45° and 1:1; the abandoned drag leaves the journal and the bytes unchanged; one `Ctrl+Z` undoes each. **If it fails** S2.
**Result** ✅ *2026-10-05, T5.* `tests/ui/tst_annotoverlay`, `[A-42]`, mouse events to the viewport over the fake backend's store: a drag on a rectangle's border is **one** `command.annot.move`, the rectangle moved by the drag's pixels in page units, one `Ctrl+Z` back; `Shift` with a mostly-right drag moves it straight right; a handle's resize is **one** `command.annot.edit`, `Shift` on a corner keeping the proportions (factors within 2 %) with the opposite corner fixed; an `Alt`+drag is **one** `command.annot.add` of a NEW annotation (a `Created` identity, the original's look, moved) with the original unmoved; `Esc` mid-drag abandons it -- the model hash and the history as they were. And **text markup is not dragged** (D6-30): a click on a highlight selects it, a drag across it moves nothing. Planted P7 (a command per mouse move) failed it.

**A-43 · `Delete`, and the flags** ✅ *2026-10-05, T5; listed hidden 2026-10-08, T9*
**Goal** D6-27.
**Steps** `Delete` on a plain annotation; on each of `annots-flags.pdf`'s flagged ones: move, resize, delete, edit text.
**Expect** Plain: deleted, one undo restores it. `Locked`: no move, resize or delete — refused with a message; `LockedContents`: text not editable; `ReadOnly`: selection only; `Hidden`/`NoView`: not drawn, listed hidden. `LockAnnotation` toggles each, and is undoable. **If it fails** S2.
**Result** *all but "listed hidden" 2026-10-05, T5; that clause at T9, the Comments pane -- below.* **Headless** (`tst_annotedit`, `[A-43][D6-27]`, on `annots-flags.pdf`): `Locked` refuses a move, a recolour and a delete by `error.edit.annotationLocked` and leaves its TEXT editable (ISO 32000-1 Table 165); `LockedContents` refuses new text (`error.edit.annotationContentsLocked`); `ReadOnly` refuses a move, new text and a delete (`error.edit.annotationReadOnly`); each context the page as a person counts it; a set holding ONE locked annotation is refused whole, nothing moved; `LockAnnotation` takes `Locked` and `ReadOnly` off (and then they move), its undo puts them back (and they hold again), and locks set by command are read back from the saved bytes with `Print` kept. **Through the widget** (`tst_annotoverlay`, `[A-43]`): `Delete` with a rectangle selected -- in Annotation tools -- deletes it as one step and one undo restores it; a locked rectangle is SELECTED and not picked up (a drag moves nothing), `Delete` on it is refused and the status bar says *locked*; the properties bar's *Locked* box takes the lock off as one `command.annot.lock`, after which it moves, and two undos restore both. `Hidden` and `NoView` are never hit (`core::hitsAnnotation`). Planted P5 (a lock not honoured) failed it. **Listed hidden, at T9** (`tst_commentspane`, `[A-43]`): the Comments pane lists a `Hidden` square among the comments, its row saying *(hidden)* (`comments.hidden`) and a visible one's not; the store's query lists Hidden and NoView unless asked not to (A-21).

**A-44 · A burst of nudges is one step** ✅ *2026-10-05, T5*
**Steps** Ten `→` presses, a pause of 600 ms, three `Ctrl+↑`.
**Expect** Two `MoveAnnotation` commands; the offsets are the plan's step sizes; arrows with **no** annotation selected scroll the page as before. **If it fails** S2.
**Result** ✅ *2026-10-05, T5.* `tst_annotoverlay`, `[A-44]`, key events: with nothing selected `→` scrolls the page; with a rectangle selected, ten `→` make **no** step until 600 ms of quiet, then **one**; three `Ctrl+↑` and the quiet, a **second** -- and the rectangle has moved **10 × 1 point right and 3 × 0.1 point up**, the step sizes the plan now names (D6-31: page units along the SCREEN's directions). A focus leaving the page commits a pending burst. Planted P8 (every nudge a step) failed it.

**A-45 · `Ctrl+E` opens one properties bar from either context** ✅ *2026-10-05, T5*
**Steps** `Ctrl+E` with the page focused; again with a tool armed; again with an annotation selected; change a colour in the bar.
**Expect** The bar toggles in the Viewer, shows the armed tool's defaults, then the selection's properties; the change is one `SetAnnotationAppearance`. **If it fails** S2.
**Result** ✅ *2026-10-05, T5.* `tst_annotoverlay`, `[A-45]`, key events: `Ctrl+E` from the Viewer (`view.togglePropertiesBar`) shows the bar with nothing to change; `Ctrl+Alt+H` puts the page in Annotation tools and the bar shows the highlighter's defaults -- changing one is the SESSION's (D6-31(c)) and constructs nothing; selecting a rectangle shows ITS properties, a colour is **one** `command.annot.setAppearance` (the rectangle blue), new text by `Enter` **one** `command.annot.edit`; `Ctrl+E` again, now Annotation tools' `annot.properties`, hides the SAME bar. The bar's words are `shell.propertiesBar.*`, in both catalogs.

**A-46 · `Esc` cancels an annotation in progress and nothing else loses it** ✅ *2026-10-08, T8, with the polygon* *(Q1)*
**Steps** `Esc` during a polygon in progress; then `Esc` in the tool finder's result popup, the find bar and the properties bar's text field.
**Expect** The polygon abandoned, the journal unchanged, the Selection tool armed; **each other `Esc` reaches its widget** — `edit.cancel` still unregistered (Stage 4's I-06). **If it fails** S2.
**Result so far** *all but the polygon 2026-10-05, T5; a polygon in progress is T8's.* `tst_annotoverlay`, `[A-46]`, key events: with the rectangle tool armed `Esc` returns to the Selection tool and the Viewer; `Esc` mid-DRAG abandons the gesture with the journal unchanged (A-42); `edit.cancel` is still not registered; the properties bar's text field takes its own `Esc` (the typing put back, the selection kept); the tool finder's `Esc` puts its list away and keeps the query, the selection kept; the find bar's closes the bar, the selection kept. Planted P13 (the field not keeping its `Esc`) failed it.
**At T8, the polygon** (`tst_drawtools`, `[A-46]`, key and mouse events): two corners of a polygon clicked, `Esc` -- the polygon ABANDONED, nothing made, the history and the model hash as they were, and the SELECTION tool armed (one `Esc`, D6-35(f)); `Enter` with nothing in progress is nothing. Planted P18 (`Esc` keeping the tool armed) failed it.

**A-47 · Pop-ups are never a tile and never a command** ✅ *2026-10-08, T8, with its key*
**Goal** D6-28.
**Steps** Open, move and close a note's pop-up; rasterize the page between; read the journal; each `commenting/popupBehaviour` value.
**Expect** The page raster **unchanged** by the pop-up; no command; `always` shows every pop-up, `on hover` on hover only, `never` none. **If it fails** S2.
**Result so far** *the three behaviours 2026-10-05, T5, through the overlay; the `commenting/popupBehaviour` key itself with the Commenting page at T8 (D6-31(d)).* `tst_annotoverlay`, `[A-47]`: `never` shows nothing, not on hover; `on hover` shows a note's pop-up while the pointer is over it and not after; `always` shows every note's and no rectangle's; a double click pins one open under any behaviour, a drag of its title moves it exactly by the drag, its cross closes it -- and through all of it **the model hash, the history and every tile request are as they were**: no command, no tile.
**At T8, the key** (`tst_drawtools`, `[A-47]`; `tst_stage6settings`): `commenting/popupBehaviour`, declared with the Commenting page (`onHover` by default, read back across two processes), IS what the overlay does -- `never`, then `always`, set in the settings and the overlay changed with it.

**A-48 · The overlay's selection is not part of the document's appearance** ✅ *2026-10-05, T5*
**Steps** Select an annotation (handles showing); take a snapshot (`S`) over it; rasterize the tile.
**Expect** No handle, no selection tint in the snapshot or the tile (D4-11, D5-11). **If it fails** S2.
**Result** ✅ *2026-10-05, T5.* `tests/composition/tst_annotink`, `[A-48]`: a rectangle of `annots-every-type.pdf` selected, its outline and handles painted; the view over a band round it holds **more than 20 pixels of the handles' accent** (the control) and a snapshot of the same band, through the Snapshot tool and the shipped engine, **none**. **And D6-2's hold** (`[D6-2]`, beside it): an edit's first paint, before any tile came back, is **the paint before it to the pixel** -- 12 superseded tiles held for 16 on their way -- and once they arrive none is held and the page shows the edit. Planted P11 (a revision dropping what is on screen) failed it.

### 5.8 Text markup and notes (T6)

**A-49 · A highlight covers the selected glyphs' ink** ✅ *2026-10-05, T6*
**Goal** Rule 6.
**Steps** `tst_annotink`: highlight a selection on `columns-two.pdf` (across the column break), `annots-rotated.pdf` and `cjk-vertical.pdf`; find the glyphs' ink in a raster of the page without the highlight, and the highlight's ink in the difference.
**Expect** Every selected glyph's ink inside the highlight; no unselected glyph's ink more than 10 % covered. **If it fails** S2.
**Result** ✅ *2026-10-05, T6.* `tests/composition/tst_markupink`, `[A-49]`: the highlighter armed by its command and DRAGGED with mouse events across the text, over the product's composition and the shipped engine; graded at 2.78 px/pt on rasters this file asks the scheduler for. A glyph's ink is the dark pixels of its box (grown across the line by a quarter) CONNECTED to ink inside the box; an ink pixel is covered when the paper on both sides of its ink -- across or along the line -- is the new mark's (the page with annotations after, against before). *"43 selected glyphs, the worst covered 1; 4480 unselected, the most covered 0"* on `columns-two.pdf` (the drag from the first column's last line, in reading order, to the second column's first -- made at zoom 0.6 so both ends are on screen); *"11 … 1; 68 … 0"* on `annots-rotated.pdf` page 2 at `/Rotate` 90; *"4 … 1; 105 … 0"* down a column of `cjk-vertical.pdf`. Each a single command. **The oracle's first run was wrong, not the product:** a glyph's region grown by a quarter caught the previous line's descender (a `g` above counted against the `e` below, 89 % and 91 %) -- found by dumping the two glyphs' pixels, and fixed by attributing ink through connectivity to the glyph's own box. Planted controls in the T6 row.

**A-50 · Underline, strikeout and squiggly sit where a reader expects** ✅ *2026-10-05, T6, after I-09*
**Steps** Each over one line of `text-a4.pdf`, against the glyphs' ink.
**Expect** Underline and squiggly below the baseline within the descender band; strikeout through the x-height's middle third. **If it fails** S2.
**Result** ✅ *2026-10-05, T6, after I-09.* `tst_markupink`, `[A-50]`: each tool dragged across a whole line of `text-a4.pdf` that holds x-height letters and descenders; the baseline and the x-height measured from the INK of the line's x-height letters (`acemnorsuvwxz`, medians of their last and first ink rows), the descender band's foot from its descenders (`gpqy`), at 2.78 px/pt; the mark's ink is what changed. Underline: baseline row 167, foot 174, the mark's rows 170-173. Strikeout: x-height 254-266, middle third 258-262, the mark centred at 259.5. Squiggly: baseline 298, foot 303, the mark's rows 299-302. **The first run failed two of the three, and the failures were the product's (I-09)**: drawn where PDFium's own generator draws them, the strikeout was centred at 257.1 (above the middle third) and the squiggle spanned rows 297-305. One row of anti-aliasing is allowed at the band's foot and none at the baseline.

**A-51 · A replace is one edit** ✅ *2026-10-05, T6*
**Steps** Select a word, *Replace text*, type a replacement.
**Expect** A `StrikeOut` and a `Caret` grouped (`/IRT`, `/RT /Group`), the replacement in the caret's `/Contents`; **one** undo step; one item in the pane. **If it fails** S2.
**Result** ✅ *2026-10-05, T6; the pane itself at T9.* `tst_markupink`, `[A-51]`: a word of `text-a4.pdf` selected with the SELECTION tool, *Replace Text* dispatched, the replacement TYPED key by key into the editor it opens and `Enter` -- nothing made before the `Enter`; then **one** journal step and two annotations: a caret holding `replacement` and a strikeout whose `/IRT` is the caret and `/RT` `Group`; the caret top-level, the strikeout NOT, the caret's group exactly the strikeout (`AnnotationStore`, which the pane will list); the text selection let go; **one** `Ctrl+Z` takes both. Saved and read back (`tst_annotmarkup`): the strikeout's `/IRT` is the object its caret was WRITTEN at -- two created annotations naming each other before either had a number (D6-33). "One item in the pane" is held at the store; the pane is T9's.

**A-52 · An insertion mark sits between the glyphs clicked** ✅ *2026-10-05, T6*
**Steps** *Insert text* at a click between two letters.
**Expect** A `Caret` whose rectangle's centre is within 1 pt of the boundary between the two glyphs' ink. **If it fails** S2.
**Result** ✅ *2026-10-05, T6.* `tst_markupink`, `[A-52]`: *Insert Text* armed, a click where the boxes of the first `ea` on `text-a4.pdf` meet, `inserted` typed and `Enter`: a caret holding it, by the login name. The boundary measured from the two letters' INK (the `e`'s last ink column and the `a`'s first, midway) and the caret's ink centre (what changed, weighted): *"0.18 pt apart"*, against 1 pt. Headless (`tst_selection`): `core::text::insertionPointAt` puts the boundary midway between the facing box edges, at the line's foot.

**A-53 · A note is made where the cursor is, by whoever is set** ✅ *2026-10-05, T6*
**Steps** `Ctrl+Alt+M` with the pointer over the page; `identity/author` empty, then set.
**Expect** A `Text` annotation and its `Popup` at the cursor; `/T` the login name `app/` passed, then the set author. **Convention 11's control:** no `core/` file reads an environment variable or the OS user name (a grep). **If it fails** S2.
**Result** ✅ *2026-10-05, T6.* `tst_markupink`, `[A-53]`, through the product's composition with `Options::loginName` fixed: `annot.stickyNote` dispatched, a click on the page, the note typed key by key -- **`keep this`, whose `k` is a letter in the editor and NOT Annotation tools' keep-the-tool** (the editor claims printable keys through Qt's `ShortcutOverride`) -- and `Enter`: a `Text` with the `Comment` icon and its `Popup`, the two naming each other, the icon's top-left corner within one device pixel of the click, `/T` the login name; `K` off, so the tool went back to Selection. `identity/author` then set to `Ada Reviewer` and the next note is hers; a third abandoned with `Esc` makes nothing. **The control** (`tst_stage6settings`, `[convention-11]`): every `.h`, `.cpp` and `.def` under `core/` -- more than 100, the case asserts -- read as text, none naming `getenv`, `qgetenv`, `qEnvironmentVariable`, `QProcessEnvironment`, `GetUserName`, `USERNAME`, `getlogin` or `getpwuid`; the rule is `core::identityAuthor`, and the name is `app::CompositionRoot::loginName`'s (`USERNAME`, else `USER`).

**A-54 · The Identity page ships** ✅ *2026-10-05, T6*
**Steps** `tst_preferences`; `PreferencesDialog::shippedPages()`.
**Expect** Identity among the shipped pages, its three keys with editors generated from the table (convention 17); the shipped count re-derived. **If it fails** S3.
**Result** ✅ *2026-10-05, T6.* `PreferencesDialog::shippedPages()` holds **ten** of 26 groups, Identity after Full Screen (`Features.md` §9.1's order): `tst_viewsettings` asserts the count (9 → 10, corrected in place) and the member; `tst_preferences`' generated-page case builds one row per declared key of every shipped group, Identity's three included, and finds every page and row label in the catalog; `tst_stage6settings` holds the three rows in the Identity group, empty by default. Labels `prefs.group.identity` and `prefs.setting.identity.*` in both shipped catalogs.

### 5.9 Free text, callout, typewriter (T7)

**A-55 · The three typed kinds are made and edited in place** ✅ *2026-10-07, T7* *(Q2)*
**Steps** A text box, a callout (with its leader), a typewriter run; each edited in place; saved; read back.
**Expect** `FreeText` with no `/IT`, `/IT /FreeTextCallout` with `/CL`, `/IT /FreeTextTypeWriter` with no border; an edit is one `EditAnnotation`; **no content stream changes** (Q2). **If it fails** S2.
**Result** ✅ *2026-10-07, T7.* **Below the tools** (`tst_annotfreetext`, `[A-55]`): a box, a callout and a typewriter made as the overlay makes them (`core::fittedFreeText` over `core::newAnnotation`), three commands, saved and read back first-party: `FreeText` with no `/IT`; `/IT /FreeTextCallout` with a six-number `/CL`, `/LE /OpenArrow` and `/RD`; `/IT /FreeTextTypeWriter` with `/BS /W 0`; each with its identity keys and `/DA` as made; and **every page's `/Contents` names the same objects and reads the same bytes** (SHA-256 per page, three pages). An edit of the text is ONE `EditAnnotationCommand`, the box grown to its lines and its `/AP` redrawn -- decoded, it says the new text; `annots-every-type.pdf`'s own box given new text is redrawn by our builder and keeps its `/DS`. **Through the tools** (`tst_typedink`, `[A-55]`, mouse and key events over the product's composition): a box DRAGGED -- its frame's top-left within 1.5 pt of where the drag began and its width the drag's; a callout PRESSED on its target and dragged to its box -- its leader's first point within 1.5 pt of the press; a typewriter CLICKED in the margin, two lines (`Shift+Enter`) -- three gestures, three commands, the tool let go after each; each box holding ink a page without annotations does not have. A DOUBLE click on the box opens its own text over it; the change is ONE more command, refitted -- width and top kept, height grown to its lines. Planted P13 (the edit not refitted) and P14 (a double click opening nothing) each failed it.

**A-56 · The characters a typed comment can show** ✅ *2026-10-07, T7* *(Q3)*
**Steps** Type into a text box: `café € “quoted”`; `中文 繁體 日本語 한국어`; `Ελληνικά`; `שלום`.
**Expect** The Latin line in Helvetica (WinAnsi), each CJK run in its predefined CID font, **non-embedded**; the Greek and Hebrew **refused at input**, the message naming the first refused character; `/DA` names the font the `/AP` uses. **If it fails** S1 if a refused character reaches the file as another; S2 otherwise.
**Result** ✅ *2026-10-07, T7, after I-10.* **The rule** (`tst_typedtext`): the first two strings accepted, `Ελληνικά` refused at U+0395, `שלום` at U+05E9, Cyrillic and anything past the BMP likewise, each named `“Ε” (U+0395)` by `error.edit.typedCharacterRefused`; kana forces Japanese and Hangul Korean, an ideograph takes the Han preference and, where that collection lacks it, the first that has it (D6-34). **The file** (`tst_annotfreetext`): five strings saved -- Latin, the four scripts under a Traditional preference, simplified Chinese, kana with an English line, Korean -- `/DA` naming each one's primary face (`Helv`, `MSung`, `STSong`, `KozMin`, `HYSMyeongJo`), held in the appearance's `/Font` and set by a `Tf`; Helvetica a `Type1` in WinAnsi with 224 `/Widths` and no descriptor; each CJK font a `Type0` in its UCS-2 CMap over a `CIDFontType0` whose INDIRECT descriptor holds no `FontFile`, `FontFile2` or `FontFile3` -- **nothing embedded**. A Greek free text is refused by the SINK by key and the update is empty; Hebrew given to a box is refused and nothing changes. **At input** (`tst_typedink`, `tst_annottools`): `Ω` typed into a box's editor goes nowhere and the refusal names `“Ω” (U+03A9)`; an input method's commit of `αβ` is refused and its preedit cleared, a CJK commit accepted; a paste holding Hebrew refused WHOLE; a note's editor refuses nothing. **The engine** (`tests/render/tst_typedtext`): every character the coverage table claims -- 59,270 at first -- written in the builder's own font objects and codes, opened by the shipped backend with the product's font hook: **PDFium read every one back as written**, and one, U+223C in Traditional, drew no ink in this machine's PMingLiU (**I-10**) -- excluded in the generator, and the table's 59,269 now all read back and draw, the ideographic space and the Hangul filler exempt as blank by definition. Planted P1 (kana and Hangul not forcing), P2 (nothing refused), P5 (U+223C claimed again) and P9 (an input method's commit unchecked) each failed it.

**A-57 · The appearance says what was typed** ✅ *2026-10-07, T7*
**Steps** For each accepted string of A-56: decode the `/AP` content stream first-party (font encoding and CMap) and compare with `/Contents`.
**Expect** Equal, character for character. **If it fails** S1.
**Result** ✅ *2026-10-07, T7.* `tst_annotfreetext`, `[A-57]`: each saved appearance tokenized by `pdf/cos/Lexer`, each `Tj` read in the encoding its `Tf` font names -- WinAnsi through a table of ISO 32000-1 Annex D written IN THE CASE, not the product's, and each UCS-2 CMap two bytes a character -- and joined: **equal to `/Contents` character for character for all five strings**, line breaks aside (a soft break keeps its space and is drawn, a hard one is where a line starts and draws nothing). Planted P3 (the euro written as WinAnsi's currency sign) failed it. Helvetica's advances, on which every line is set, are held to the engine apart: its own metrics (Foxit Sans) equal the table on 213 of 217 codes and differ on exactly the euro, `‹`, `«` and `»` -- where Helvetica's AFM and Arial agree with the table -- and in the font the builder writes, with its `/Widths`, the engine sets every code on the table's advance (D6-34(d)); planted P4 (no `/Widths`) failed it.

### 5.10 Drawing, ink, eraser, tool defaults (T8)

**A-58 · Shapes and their constraints** ✅ *2026-10-08, T8*
**Steps** Line (each head style at each end), arrow, rectangle, oval, polygon and polyline (finished by `Enter`), cloud (two jag sizes); each with `Shift`.
**Expect** The right subtype and `/LE`, `/BE`, `/Vertices`; `Shift` gives 45° steps, a square and a circle; `Enter` finishes, `Esc` abandons. **If it fails** S2.
**Result** ✅ *2026-10-08, T8.* **Through the widgets** (`tst_drawtools`, `[A-58]`, mouse and key events): a rectangle dragged with `Ctrl+Alt+R` -- ONE command, its rectangle the drag's to 0.01 pt, `/BS /S` solid, the tool's colour -- and the tool let go; a CLICK with it makes nothing; an oval with `Shift` a CIRCLE (width equals height), `Shift` not the temporary hand; a line with `Shift` at 45 degrees; the arrow (`annot.arrow`) a Line ending `/LE [/None /OpenArrow]`; a polygon of three clicks and `Enter`, a polyline of four the last a DOUBLE click (four vertices, not five), a cloud (`annot.cloud`) of four and `Enter` -- a Polygon with `/BE` intensity 1 and `/IT /PolygonCloud`, its rectangle holding the bumps; `Shift` on a polygon's corner keeps the edge at 45 degrees. **The two jag sizes** are intensity 1 (the tool's) and 2 (the bar's, `tst_annotdrawing`'s refit case: the larger bumps fit). **In the file** (`tst_annotdrawing`, `[A-58]`): each kind's keys read back -- `/L`, `/LE` (and none on a plain line), `/IC`, `/CA`, `/BM`, `/Vertices`, `/BE << /S /C /I >>` with no `/RD` (ISO 32000-1 Table 178 gives a polygon none), `/InkList`. Planted P9 (`Shift` the temporary hand) and P10 (a double click not finishing) each failed it.

**A-59 · Ink passes through every sample and keeps them** ✅ *2026-10-08, T8*
**Goal** D6-15.
**Steps** `tst_annotation`: stroke sample sets with sharp turns, loops and repeated points.
**Expect** `/InkList` equals the samples (after the 0.25 px deduplication); the drawn curve passes through every sample within 0.01 pt and stays inside the rectangle grown by half the width. **If it fails** S2.
**Result** ✅ *2026-10-08, T8.* `tst_drawnshapes`, `[A-59]` -- in `rawpage_tests`, not `tst_annotation`: a zigzag of sharp turns, a loop past a full turn, repeated points, a dot and three right angles, deduplicated as the tool does (the repeats went and nothing else did), at widths 1 and 6: `/InkList` IS the samples kept; the BUILDER's stream, read back with `pdf/cos/Lexer` (not `core::inkCurve`, which produced it), starts or ends a piece at every sample within 0.01 pt, and every point of every cubic, sampled fifty times, lies inside the rectangle LESS half the stroke -- so the stroke is inside the rectangle. A hairpin does not overshoot its turn by more than a fifth of the chord (centripetal). Planted P1 (the curve missing its samples by 0.05 pt) failed it; **planted P2 (the rectangle fitted to the samples alone, not the curve's control points) STAYED GREEN on its first run** -- no stroke curved more than a point past its samples' box -- and the case gained three right angles, whose curve bulges 12.5 points past them, after which P2 failed it (the T8 row).

**A-60 · The eraser splits strokes** ✅ *2026-10-08, T8*
**Steps** Erase a whole stroke; erase the middle of another.
**Expect** The first removed; the second split into two strokes at the covered samples; **one** `EditAnnotation`; undo restores the samples exactly. **If it fails** S2.
**Result** ✅ *2026-10-08, T8.* **The geometry** (`tst_drawnshapes`, `[A-60]`): a whole stroke under the eraser goes; one erased through its middle splits in two at exactly the covered samples; a sweep crossing a stroke BETWEEN two samples splits it there and removes none; a one-sample fragment goes, an untouched dot stays, nothing touched changes nothing. **The command** (`tst_annotdrawing`, `[A-60]`): the first stroke gone and the second in two -- ONE `EditAnnotationCommand`, one step; undone, the model hash and the very samples back; saved and read back two strokes. **Through the tools** (`tst_drawtools`, `[A-60]`): a pencil stroke of twenty-one moves is ONE `Ink` whose `/InkList` is those moves' page points; the eraser (`annot.eraser`) dragged through its middle is ONE step leaving two strokes, the eraser still armed; undone, the samples exactly; erased whole, the ink deleted. Planted P3 (no split where the eraser crossed) and P17 (the whole ink deleted) each failed it.

**A-61 · Tool defaults are a declared family** ✅ *2026-10-08, T8*
**Goal** D6-13.
**Steps** `Ctrl+Alt+Enter` with a tool armed and changed properties; a new annotation; the Commenting page; `ctest -R "unit.settings.compile"` with a fixture naming an undeclared `(tool, property)`.
**Expect** The armed tool's family rows written; the next annotation uses them; the page edits them; **the fixture does not compile**. **If it fails** S2.
**Result** ✅ *2026-10-08, T8.* **Through the widgets** (`tst_drawtools`, `[A-61]`): the rectangle tool armed and `Ctrl+E`: the bar shows its properties -- width and line style enabled, font size NOT (`ToolDefaults.def` gives a rectangle none); blue, 4 points, dashed set on the bar are the SESSION's (the settings unchanged); `Ctrl+Alt+Enter` writes them -- `commenting/default{Colour,Thickness,LineStyle}.rectangle` -- and clears the session change; the next rectangle is blue, 4 points, `/BS /S /D` with the 4-point dash. The Commenting page: its generated rows exactly `commenting/stickyTool` and `commenting/popupBehaviour`, and below them the tool editor, showing what `Ctrl+Alt+Enter` wrote, its font size greyed; a fill chosen there is written and the next rectangle filled with it; its reset takes the tool back to the table. **The family** (`tst_stage6settings`): every row's default accepted by its validator, every key's path resolved both ways and declared, family-shaped paths that name nothing refused (a pencil's font size, an eraser's colour, an unknown tool or property, a wrong case); a tool's rows written and read back across two processes, a property it does not have never written, a hand-edited value out of range read as the row's default, a reset removing the rows. **`unit.settings.compile.undeclared_tool_default`**: `ToolDefaultKey::of<Pencil, FontSize>()` DOES NOT COMPILE, the error naming `ToolDefaults.def`; the control (`declared-key.cpp`) names a declared pair and compiles. Planted P11 (`Ctrl+Alt+Enter` writing the table), P13 (a path resolved without asking the table) and P14 (every pair declared -- the fixture compiled) each failed it.

**A-62 · `K` keeps the tool** ✅ *2026-10-08, T8*
**Steps** `K`; draw two rectangles; `K`; draw one.
**Expect** `commenting/stickyTool` toggled; the tool stays armed after the first two and returns to Select after the third. **If it fails** S3.
**Result** ✅ *2026-10-08, T8.* `tst_drawtools`, `[A-62]`, key events: `K` with the rectangle tool armed sets `commenting/stickyTool`; two rectangles drawn and the tool still armed; `K` clears it and the third returns to the Selection tool; the key set directly reaches the open view (`DocumentWindow::applyCommentingSettings`). Planted P12 (`K` toggling the view and not the setting) failed it.

**A-63 · Opacity and blend mode reach the pixels** ✅ *2026-10-08, T8*
**Steps** A highlight at 50 % opacity and `Multiply`; read its `/AP`'s `ExtGState`; rasterize over black text.
**Expect** `/CA`, `/ca` and `/BM /Multiply` present; the text's ink stays dark under the highlight (multiply), lighter colours tinted. **If it fails** S2.
**Result** ✅ *2026-10-08, T8.* **The stream** (`tst_annotdrawing`, `[A-63]`): a highlight at 50 % -- its graphics state `/CA 0.5`, `/ca 0.5`, `/BM /Multiply`, and its dictionary NO `/BM` (the kind's own, D6-35(g)); told `Normal`, no blend in the state and `/BM /Normal` in the dictionary; a rectangle under `Screen` blends in its state and says so; an unknown blend refused by key; saved and read back. **The pixels** (`tst_drawink`, `[A-63]`): over a line of `text-a4.pdf`'s body text, *"4100 of 4100 glyph pixels still dark, 19218 of 19218 paper pixels yellow; the Normal control: 0 of 4594 still dark"* -- the control, the same highlight at full opacity told `Normal`, covers the glyphs, so the measure tells the two apart. Planted P7 (the builder ignoring a named blend) failed it.

### 5.11 Appearances (T8, extended by every later tool task)

**A-64 · Every appearance matches its golden** ✅ *2026-10-08, T8*
**Steps** `ctest -R "render.golden.appearance"`.
**Expect** Every type × property case inside `golden/README.md`'s thresholds — colours, opacity, width, dash, blend mode, each page rotation and `NoRotate`; every baseline's first capture LOOKED AT and recorded in `golden/UPDATES.md`. **If it fails** S2; the diff image in `golden-failures/` first.
**Result** ✅ *2026-10-08, T8.* `render.golden.appearance every case matches its baseline` and `... every case has a baseline and every baseline is a case`: **31 cases** of `tests/golden/AppearanceCases` -- a declarative spec of 200 by 150 point pages with a grey band and a black bar of content, each annotation at a fixed identity and instant -- rendered at 144 dpi and compared with `golden/appearance/<name>.png` at the corpus suite's thresholds, all inside them; the corpus baselines unchanged. The cases: a solid, a dashed and an arrowed line; all ten endings, filled and not, straight and slanted; rectangles solid-filled, dashed and dotted; ovals filled and dashed; a filled pentagon; a polyline with a circle and a closed arrow; clouds of intensity 1 and 2 and a producer's cloudy rectangle; thin and thick ink, a dot among them; a highlight under its multiply and told `Normal`; an underline, a strikeout, a squiggly; a caret and both note icons; 50 % opacity; multiply and screen over the content; a page at `/Rotate` 90 and a `NoRotate` note on one; a text box, a callout ending in a diamond, a typewriter. **Every first capture was LOOKED AT** (contact sheets, and the turned page alone) **before it was kept -- and one was not kept**: `polyline-endings` had NO endings, because the case's spec never set `/LE`; the spec was corrected and that one recaptured, both recorded in `golden/UPDATES.md`. Planted P16 (a line's start ending not drawn) failed it.

**A-65 · Our appearance agrees with PDFium's own** ✅ *2026-10-08, T8, after I-12*
**Goal** §3.3's second oracle.
**Steps** `tst_appearance`'s control over the eight types PDFium can generate — not line (I-02).
**Expect** **Ink IoU ≥ 0.90** for each. **If it fails** S2 — and the case says which drew what.
**Result** ✅ *2026-10-08, T8, after I-12.* `tests/render/tst_appearance`, `[A-65]`: each annotation written twice by `appearancePdf` -- with our form, and as the same dictionary with no `/AP`, which PDFium 153 generates for at load -- drawn at 144 dpi; a pixel is ink when it moved from the page drawn without annotations by at least half as far as the annotation's colour is from it. *"highlight IoU 1, underline 1, square 1, circle 1, ink 0.971, note 0.999"* -- six graded, all past 0.90. **Strikeout (IoU 0) and squiggly (0.29) are drawn and PDFium's control is not empty, and they are not graded here** (D6-32, I-09: graded by A-50 against the glyphs). **The first run failed two, and both were read before anything moved:** the underline at 0.20, because the case drew a 26-point-tall quad and our rule grows the line past 14 points where PDFium keeps 1 point (D6-32) -- the case now grades at body-text height, 12 points, where the rule claims PDFium's line, and says why; and **the note at 0.667 with `Note`, then 0.877 with `Comment`** -- PDFium generates ITS OWN speech bubble for every note whatever `/Name` says, so `Note` compared two icons; `Comment`, which RawPage's tool writes, overlapped at 0.877, below the bar fixed before the run: **I-12**, fixed in the product, not in the case. Planted P15 (T6's bubble back) failed it.

**A-66 · Every generated stream is well-formed** ✅ *2026-10-08, T8, for the drawing kinds; each later kind with its task*
**Steps** Every `/AP` the suite generates, re-read by `pdf/cos/`'s lexer.
**Expect** Only operators in the allowlist; `q`/`Q` and `BT`/`ET` balanced; every resource named in the stream present in its `/Resources`. **If it fails** S1 — a malformed stream is drawn differently by every reader.
**Result** ✅ *2026-10-08, T8.* `tst_annotdrawing`, `[A-66]`: more than 150 appearances -- lines and polylines with every ending at each end, filled and not, in each line style; squares and circles filled and not in each style; a polygon under each of the nine blends, at three cloud intensities, and a cloudy rectangle under each; an ink of a dot, a line and a curve, dashed -- each passing `ContentStreamBuilder::check` (the operator allowlist, balanced `q`/`Q` and `BT`/`ET`), every `gs` and `Tf` operand present in the form's own `/Resources`, and no object written beside the form. (Free text's, with its fonts, are T7's A-57 case; the markup kinds' T3's and T6's.)

### 5.12 The Comments pane (T9)

**A-67 · Group, sort and filter are the label's** ✅ *2026-10-08, T9*
**Steps** `tst_annotstore` (headless) and `tst_commentspane` over `annots-threads.pdf`.
**Expect** Each grouping — page, author, date, type, status, checkmark — and each sort within it; each filter — author, type, status, checkmark, date range — returns **exactly** the label's set; the pane shows what the query returns. **If it fails** S2.
**Result** ✅ *2026-10-08, T9.* **Headless** (`tests/unit/tst_annotthreads`, `[A-67]` -- a new file, not `tst_annotstore`): `annots-threads.pdf` read first-party and graded on `labels/annots-threads.json` (rule 7): for each of the SEVEN groupings (none, page, author, date, type, status, checkmark) under each of the FOUR sorts, every top-level comment -- the label's four roots and its four controls -- **exactly once**, under the key the label gives it (a page, an author, a UTC day, a subtype name, the latest Review state or `None`, Ana's checkmark), the groups in their order (pages up, authors alphabetically, days newest first, the statuses in Table 172's order with `None` last, `Marked` first) and each group sorted; a day is the HOST's calendar day (note 24, written at 02:00 UTC, is the 15th five hours west). Every filter returns exactly the label's set: one and two authors, one and two types, each status (Ben's *Accepted* on note 6 was a status once and is not now: the filter asks for the current one), each identity's checkmark both ways (Zoë's latest on the highlight is *Unmarked*), a date range inclusive at both ends, TEXT found in a reply under a comment (*last week* is only in Ben's reply to note 6) and never in a state reply's (*set by*), and the filters together. **Through the widgets** (`tst_commentspane`, `[A-67]`): the pane grouped by author from its combo box lists exactly the store's grouped query, three groups; its filter menu's author entry narrows to Ben's comment; status and date-range filters likewise; nothing the view does is a command. Planted P1 (days oldest first), P2 (a reply's text not searched) and P3 (the status the FIRST, not the latest) each failed it.

**A-68 · Replies make threads** ✅ *2026-10-08, T9*
**Steps** `Ctrl+Enter` on a comment; again on the reply; type; save; read back.
**Expect** `ReplyToAnnotation`, `/IRT` to the parent, `/RT /R`; shown nested two deep. **If it fails** S2.
**Result** ✅ *2026-10-08, T9.* **Headless** (`tst_annotthreads`, `[A-68]`): a reply to `annots-threads.pdf`'s highlight -- a note-sized icon 52 points in from the page's right edge at the highlight's height (D6-36(a)) -- and a reply to it on the reply's own icon, two `ReplyToAnnotationCommand`s (`command.annot.reply`): the second's parent is the first, the first's the highlight, neither a new item of the list (still eight); saved and read back first-party, `/IRT` is object 15 and then the OBJECT the first reply was written at, no `/RT` is written in the update (`/R` is the default), author and text as typed, each drawn. **Refused where it must be** (`[D6-27][D6-36]`): a reply to a `ReadOnly` comment by `error.edit.annotationReadOnly`, to a direct dictionary by `error.edit.annotationDirect` (**I-14**), a `Locked` one accepted; a reply placed on another page than its parent, a state handed to the reply command and a reply to the status command each made nothing. **Through the widgets** (`tst_commentspane`, `[A-70]`): `Ctrl+Enter` opens the editor below the list, the text TYPED key by key -- nothing made until `Enter` -- then ONE reply under the highlight by the current identity. The two commands' serialization and revert: `tst_command_serialize` (both samples, and thirteen hostile shapes each refused by `Result`) and the reversibility sweep, now *1745 combinations over 100 editable corpus files, 0 failures*. Planted P5 (a reply written in a group) and P14 (a direct parent not refused) each failed it.

**A-69 · Status, history and the personal checkmark** ✅ *2026-10-08, T9*
**Goal** D6-12.
**Steps** `Ctrl+Shift+K` → *Accepted*, then *Completed*; `Space`; change `identity/author`; `Space` again.
**Expect** Two `/StateModel /Review` replies with author and date; the history lists both; the first `Space` adds a `/Marked` reply by the first author, and the second author **does not see it as theirs** — their checkmark is independent. **If it fails** S2.
**Result** ✅ *2026-10-08, T9.* **Headless** (`tst_annotthreads`, `[A-69]`): on the caret, *Accepted* then *Completed* by Ada -- two `SetAnnotationStatusCommand`s (`command.annot.setStatus`), the history both in order with author and instant, the status *Completed*; Ada's checkmark, then Ben's, then Ada taking hers off (`command.annot.checkmark`) -- each identity's own at every step. Saved and read back: five hidden state replies, flags 30, **no `/AP`** and no `/Contents` (D6-36(b)), the same history and checkmarks; five undos and the caret has none. **Through the widgets** (`tst_commentspane`, `[A-69][A-70]`): `Space` sets the current identity's checkmark and the row's box shows it; `identity/author` changed, the same row shows UNchecked for the new identity, whose `Space` is his own and whose second `Space` takes only his off; `Ctrl+Shift+K` opens the five states and one chosen is one command, the row's status column saying it. Planted P6 (`Marked` allowed in the Review model) failed the hostile serialization case; P7 (a state reply drawn) and P16 (the checkmark not the identity's) each failed theirs.

**A-70 · The whole Comments pane table, from key events** ✅ *2026-10-08, T9* *(Q1)*
**Steps** `tst_commentspane`: `↑`/`↓`, `Enter`, `F2`, `Ctrl+Enter`, `Delete`, `Space`, `Ctrl+Shift+K`, `Ctrl+F`, `Ctrl+.`/`Ctrl+,` in the pane; `Ctrl+.`/`Ctrl+,` and `Space` in the Viewer; `Ctrl+Shift+C`.
**Expect** Each acts as §8 says; `Ctrl+.` from the pane **moves the page view** to the comment; **`Space` in the Viewer still scrolls** (the collision's control); `Ctrl+Shift+C` opens the pane from anywhere. **If it fails** S2.
**Result** ✅ *2026-10-08, T9.* `tests/ui/tst_commentspane`, KEY EVENTS through the application (the window's `ShortcutBinder` sees each first), over the fake backend's store: `Ctrl+Shift+C` from the page opens the pane focused, in `KeyContext::Comments`, and **moves nothing** -- no selection on the page, the page where it was (**I-15**); the list is the four top-level comments, the reply under its note; `Down` walks into the thread and on, and the PAGE FOLLOWS -- the comment selected on it; `Enter` opens the comment's own editor, the properties bar's text focused and holding it; `F2` edits below the list, a typed SPACE a space and not the checkmark, `Enter` ONE `command.annot.edit`; `Ctrl+Enter` ONE reply; `Space` ONE checkmark; `Ctrl+Shift+K` the status menu, a state chosen ONE command; `Ctrl+F` the pane's search field, typing narrowing the list; `Ctrl+.` steps to page 2's note and the page view goes there, again wraps to the first, `Ctrl+,` back the other way; `Delete` the note with its pop-up, reply and status as ONE step, one undo back. **In the Viewer**: `Space` scrolls a screen and constructs nothing (the three-way collision's control); `Ctrl+.` with nothing selected goes to the first comment on or after the page the reader is on and selects it, and the pane's current comment follows. An editor in the pane keeps its own `Delete` and `Esc` -- the comment not deleted, the reply abandoned. The census edits this moved: Viewer rows registered 50 → 52 and unregistered 3 → 1 (`Ctrl+B`, Stage 7's), the unbuilt pane chords seven → six. Planted P10 (the page not following), P11 (`Delete` without the thread) and P13 (opening the pane moving the page) each failed it.

**A-71 · Deleting many is one step** ✅ *2026-10-08, T9*
**Steps** *Delete all*; undo; filter by an author, *Delete matching*; undo.
**Expect** One `DeleteAnnotation` each, removing exactly the set (replies with their parents); one undo each restores all. **If it fails** S1 if an undo loses one; S2 otherwise.
**Result** ✅ *2026-10-08, T9.* **Headless** (`tst_annotthreads`, `[A-71]`): `AnnotationStore::threadOf` is the LABEL's thread for each of the four roots -- the comment, its replies at any depth, every state and group member, and the pop-up of each (nine for note 6) -- a reply the store shows top-level (the one from page 2) an item of its own; a pop-up named only by its note's `/Popup`, and one naming its note only by its own `/Parent`, each in its note's thread; ONE `DeleteAnnotationCommand` takes note 6's thread, a second everything, and two undos give the model hash back. **Through the widgets** (`tst_commentspane`, `[A-71]`): Ana's comments filtered, *Delete matching* (`comment.deleteMatching`, minted) ONE step taking her two and note 10's pop-up, reply and status, the highlight kept; *Delete all* ONE step taking everything; one undo each restores the count. Planted P4 (no pop-up by `/Popup`) **stayed green on its first run** -- the threads file names every pop-up both ways -- and the one-direction case was added, after which P4 and P4b (no pop-up by `/Parent`) each failed it; P15 (*Delete matching* deleting all) failed it.

**A-72 · Sequence numbers are shown and never written** ✅ *2026-10-08, T9*
**Steps** Turn numbering on; rasterize a tile; read the journal.
**Expect** Numbers in the pane and over the page in reading order; **tile pixels unchanged**; no command. **If it fails** S2.
**Result** ✅ *2026-10-08, T9.* **Headless** (`tst_annotthreads`, `[A-72]`): `AnnotationStore::sequenceNumbers` in reading order by hand values -- top left, top right, lower left, then page 2 -- and with page 1 shown at 90 and at 180 degrees the order its turned rectangles give; on `annots-threads.pdf` exactly the label's eight top-level comments numbered 1 to 8 and no reply, pop-up or status. **Through the widgets** (`tst_commentspane`, `[A-72]`): `comment.numbering` (minted, checkable) puts 1 … 4 in the list's rows and the SAME map on the page's overlay, with no command and the model hash unchanged; a status added while numbering is on is not numbered; off, the overlay holds none. **In pixels** (`tests/composition/tst_commentscale`, `[A-72]`): on `annots-threads.pdf` through the shipped engine, the view's grab gains the badges (more than 20 pixels differ) while the page's raster, asked for fresh, differs from before by **0 pixels**, and the journal is as it was. Planted P8 (numbers ignoring the rotation) failed it.

**A-73 · The pane at 9,000 annotations** ✅ *2026-10-08, T9*
**Goal** §3.6.
**Steps** `tst_commentspane`'s timing case over `annots-900.pdf`, three runs.
**Expect** Inside every §3.6 bound, the figures printed. **If it fails** S2.
**Result** ✅ *2026-10-08, T9.* **Deviation: the case is `tests/composition/tst_commentscale`'s**, not `tst_commentspane`'s -- it needs the real file read by the first-party reader and the product composed over the engine, which the widget suite links none of. On `x64-release`, three runs within the case: *"store hydrated 88-97 ms, live heap 20.2 MB; pane populated 56-59 ms; 60 changes p95 26.0 ms, max 26.8 ms"* (two runs of the case) -- each change of group, sort or filter timed from the call to the list PAINTED, every filter taken off again at the end (9,000 listed). Against §3.6: hydration ≤ 500 ms, live heap ≤ 64 MB (LIVE heap, `tests/bench/HeapCensus`, never private bytes -- Stage 2's I-35), population ≤ 500 ms, change p95 ≤ 100 ms -- every one inside. **The unoptimized `x64-debug` reader hydrates in 730-820 ms**: the case grades the shipped build on the guide's 500 and the debug build on a 2 s ceiling of its own, and says why at the assertion (A-22's precedent).

**A-74 · Comments are searched from the store** ✅ *all but the attachment clause 2026-10-08, T9; that clause 2026-10-12, T11*
**Goal** D6-11.
**Steps** Add a note containing `marmoset` without saving; find `marmoset` with `search/includeComments` on, then off; Advanced Search over a scratch folder holding the same file, closed; a find hit on an attachment of `attachments.pdf`.
**Expect** Found when on, not when off; the hit **selects the annotation**; the folder search finds only what is saved; the attachment hit **selects the attachment in the Attachments pane**. **If it fails** S2.
**Result so far** *2026-10-08, T9.* **Through the product** (`tests/composition/tst_commentscale`, `[A-74]`): on `text-a4.pdf`, a note made as the sticky-note tool makes one, holding *A marmoset was here*, and a highlight carrying *Quokkas, not marmosets* -- neither saved; the find bar, typed into with `search/includeComments` on, finds both (two hits, both of the Comments class) and the current one's annotation is SELECTED on the page; *Quokkas* finds the highlight alone and selects it; with the switch off, nothing. **Headless** (`tst_annotthreads`, `[D6-11][A-74]`): an open document's comment units are its store's -- every comment with text, replies included, no state reply, each unit's locator the annotation's identity -- and an unsaved note is among them at once, found by the matcher the run uses; the file's bytes hold no *marmoset*. **The folder** (`tests/render/tst_foldersearch`, `[A-74]`): a scratch folder holding a copy of `text-a4.pdf` with that note made and NOT saved: Advanced Search finds **0**; saved, **1** -- a folder search reads the FILE. Planted P9 (a run not handed the store's comments) failed it. **The attachment clause, 2026-10-12, T11** (`tests/composition/tst_attachmentproduct`, `[A-74]`, the REAL find bar over the real engine): `attachments.pdf` with `search/includeAttachments` on, *readme* typed into the find bar -- the first hit is of the Attachments class, and the Attachments pane is brought in front with `readme.txt` its current file; no tab opened, nothing written. Planted P11 (the hit not passed on) failed it.

### 5.13 Stamps (T10)

**A-75 · Built-in stamps are what other readers know** ✅ *2026-10-09, T10*
**Steps** Place each built-in standard stamp.
**Expect** `/Name` one of ISO 32000-1 Table 181's values; the `/AP` golden. **If it fails** S2.
**Result** ✅ *2026-10-09, T10.* **The file** (`tests/unit/tst_stamps`, `[A-75]`): all fourteen of Table 181's names placed as one command on `text-a4.pdf`, saved and read back first-party: each `/Name` the name placed and in Table 181, its `/C`, its form well-formed with every `gs`, `Tf` and `Do` resolving in its own resources, its `/BBox` the stamp's natural box and no `/Matrix` unturned, its words in its stream, and its font base-14 `Helvetica-Bold` with 59 `/Widths` (codes 32 to 90). **The baselines** (`render.golden.appearance`, `golden/appearance/`): `stamps-standard-1` … `5` (the fourteen, three to a page), `stamps-dynamic`, `stamps-turned` (30 and 90 degrees) and `stamp-image` -- eight new, **each LOOKED AT before it was kept** (`golden/UPDATES.md`, 2026-10-09T08:11:38Z), 39 appearance baselines in all, none of T8's 31 regenerated. Planted P15 (`/Widths` lost) failed it -- see the T10 row for the list.

**A-76 · A dynamic stamp is baked, not computed** ✅ *2026-10-09, T10*
**Steps** With the clock seam fixed at 2026-10-04 09:30, place a dynamic stamp; save; reopen with the clock at 2027-01-01.
**Expect** The author, date and time of the placement in the `/AP` and `/Contents`, unchanged on reopening. **If it fails** S2.
**Result** ✅ *2026-10-09, T10.* `tst_stamps`, `[A-76]`: `DynamicApproved` placed at 2026-10-04T09:30:00Z, the host's offset 0: `/Contents` *"Stamp Tester\n2026-10-04 09:30"*, saved; read back by the first-party reader -- which has no clock at all -- with the same `/Contents`, its appearance holding both lines (hex in the stream); and **redrawn** in a session opened from the saved file -- a turn of 90 degrees, a quarter of a year's clock later by construction -- drawing the same baked lines. The lines' arithmetic by hand values: +8 hours, -5 hours across midnight AND a year (2027-01-01T00:00Z at UTC-5 is *2026-12-31 19:00*), an author of white space left out. **And an edit of the text is an edit of the lines** (D6-37(d)): a dynamic stamp read from a file given new `/Contents` draws them, the old author gone. **Deviation:** the clock is not a seam the reader reads -- a file has no clock -- so *"reopen with the clock at 2027-01-01"* is a reopen with no clock and a redraw with none consulted; what is checked is that nothing the redraw draws comes from a clock. Planted P4 (the lines drawn from the author and not from `/Contents`) failed it.

**A-77 · Image stamps keep their pixels** ✅ *2026-10-09, T10*
**Steps** Custom stamps from `corpus/stamps/*.png` (with alpha) and `*.jpg`.
**Expect** The PNG as a Flate image with an `/SMask`; the JPEG as `/DCTDecode` **byte-identical** to the file's; each rendered within the golden threshold of the source image. **If it fails** S2.
**Result** ✅ *2026-10-09, T10.* **The file** (`tst_stamps`, `[A-77]`): the corpus JPEG written as an image XObject `/DCTDecode`, 160 wide, `DeviceRGB`, no mask, its stream **byte-identical** to `corpus/stamps/signature.jpg`; pixels with an alpha written `/FlateDecode`, their stream decompressing to the samples given and their `/SMask` (`DeviceGray`) to the alpha given; an opaque alpha is dropped. A JPEG `/DCTDecode` cannot read is refused by key in `core/` -- lossless (SOF3), CMYK, twelve bits, a truncated file -- and `ui/` decodes such a file as pixels instead. **The ink** (`tests/composition/tst_stampink`, `[A-77]`): each corpus image placed at one point a pixel on `text-letter.pdf` at ONE DEVICE PIXEL A POINT, rendered by the product's engine and compared with the SOURCE as Qt decodes it, the PNG composited over the page as drawn without annotations: *"stamps/seal-rgba.png: 0 % of its pixels above 8, 0 % above 96, the worst 1"*, *"stamps/signature.jpg: 0 % … the worst 0"* -- inside both golden thresholds (8 per channel on 0.1 %, 96 on 0.01 %). **Deviation found measuring it:** the case was first written on `text-a4.pdf`, an A4 page 841.89 points tall, which the engine draws into 842 rows -- a stretch that resamples every image on it (the seal 9.7 % above 8, the signature 7.2 %; with image smoothing off still 6 % and 5 %, so not smoothing); on a page of whole points (US Letter) the same images are exact. The case says why at its top. Planted P1 (a JPEG written Flate) and P3 (the mask dropped) failed it.

**A-78 · One image, placed twice, is written once — and a PDF is not a stamp source** ✅ *2026-10-09, T10*
*(Rewritten 2026-10-04 by Q7's answer: until then it graded `ObjectGraphCopier`, which moved to Stage 7.)*
**Steps** A custom stamp from `corpus/stamps/*.png`, placed on two pages; save; count the image XObjects. Then offer the custom-stamp dialog a PDF.
**Expect** **One** image XObject (with its `/SMask`) referenced by both appearances; the dialog lists image types only and refuses a PDF by name. **If it fails** S2 for a duplicate; S3 for the dialog.
**Result** ✅ *2026-10-09, T10.* **The file** (`tst_stamps`, `[A-78]`): one image (pixels with an alpha) placed on pages 1 and 2 by TWO commands, as two clicks make them, and one save: two appearances naming the SAME `Im0` object, and the whole update holding exactly two image XObjects -- the picture and its mask. (Within one save, as a font is: a later save writes the image again, D6-37(e).) **The dialog** (`tests/ui/tst_stamps`, `[A-78]`): its browser's filter names `*.png`, `*.jpg`, `*.jpeg`, `*.bmp`, `*.gif` and no PDF; `text-a4.pdf` offered as a typed path is refused naming *text-a4.pdf*, OK disabled, nothing to make; the PNG is kept as its own bytes, a BMP as a PNG of itself; drawn and typed initials are PNGs with a transparent ground, filed under Signatures. **The library** refuses a PDF's bytes too, by the name given (`tst_profilestore`, `error.stamps.notAnImage`), writing nothing. Planted P2 (an image keyed per stamp) and P11 (the dialog taking a PDF) failed it.

**A-79 · The stamp library survives a restart and damage** ✅ *2026-10-09, T10*
**Steps** `tst_profilestore` with a scratch root: a category, a favourite, two processes; then a truncated profile file.
**Expect** Both survive the second process; the damaged file is **reported** and the rest of the library loads. **If it fails** S2.
**Result** ✅ *2026-10-09, T10.* **Two processes** (`unit.profile_store.restart`, `tests/unit/RunProfileRestart.cmake`): one run of `rawpage_tests` writes a stamp in a category of its own, a favourite SIGNATURE and a favourite built-in (`NotApproved`) under a scratch root -- five files -- and a second run reads every one back, the signature's image byte for byte; **the control**, the reader over an empty root, fails, as it must. **Damage** (`tst_profilestore`, `[A-79]`): a description TRUNCATED to half, one whose image was deleted, one with a category no person can type and a truncated `favourites.json` are each REPORTED by file name -- four -- and the one sound stamp loads, still a favourite; the damaged favourites are lost, not invented. **Through the window** (`tests/ui/tst_stamps`): a custom stamp added is listed under its category and Favourites, chosen with its pixels decoded once, refiled and unfavoured (each WRITTEN at once, as a second store over the root reads), and deleted; a built-in may be a favourite and is neither refiled nor deleted. **No suite reads or writes the tester's stamps**: the product's root is `app/main.cpp`'s alone (D6-37(g)). Planted P8 (a damaged file stopping the load), P9 (a description naming a missing image loaded), P10 (the favourites never written) and P12 (a custom stamp chosen without its pixels) failed it.

**A-80 · A placed stamp resizes and rotates** ✅ *2026-10-09, T10*
**Steps** Resize by a handle; rotate by 90° and 30°.
**Expect** One command each; the `/AP` matrix carries the rotation, and the rectangle bounds the rotated ink (rule 6). **If it fails** S2.
**Result** ✅ *2026-10-09, T10.* **The file** (`tst_stamps`, `[A-80]`): an `Approved` stamp saved, RESIZED by 1.5 -- one command, and its appearance object the SAME one (a stamp is a picture, D6-37(b)) -- then TURNED 30 degrees: a new form whose `/Matrix` is *cos 30, sin 30, -sin 30, cos 30*, `/Rotate 30`, and a rectangle the turned box's bounds at the 1.5 scale (to 0.02 points); the undo takes the turn back and leaves the resize. The geometry by hand: a quarter turn swaps the sides at the same scale, -330 is 30, a turn back to 0 is the box again. **The ink** (`tests/composition/tst_stampink`, `[A-80]`): an `Approved` stamp on `text-a4.pdf`, selected with a click and RESIZED by dragging its bottom-right handle, then TURNED 30 degrees from the properties bar's turn -- one command each; the page drawn with and without annotations: *"the turned stamp's ink 243 x 182 px inside its rectangle 250.697 x 189.164 px"* -- every pixel of ink inside the rectangle (a pixel's rounding allowed) and reaching across at least 90 % of it each way -- **and NOT SQUEEZED**: *"unturned ink 246 x 78 px; turned, expected 243.595 x 182.103 px"*, the turned ink the MEASURED unturned ink turned (a rounded rectangle's turned extent, its outer corner radius taken off), within 2 pixels. The bar's turn is disabled for a square (`tests/ui/tst_stamps`). Planted P6 (the matrix turned the wrong way), P7 (a turn keeping the unturned rectangle) and P18 (the bar's turn not offered) failed it -- **P7 only after the ink case was strengthened**: a form's turned box is mapped onto WHATEVER rectangle it has (ISO 32000-1 Algorithm 8.1), so a wrong rectangle squeezes the ink inside itself, and *ink inside the rectangle* alone stayed green over the plant (the headless case caught it). The measured comparison above is the strengthening.

**A-81 · Copy, cut and paste carry annotations** ✅ *the image clause and every text widget's `Ctrl+V` 2026-10-09, T10; the annotations, the cut, the bytes a second process would have and every text widget's `Ctrl+X` 2026-10-16, T13*
**Since T14 (2026-10-16):** a pushpin and a sound READ from `annots-every-type.pdf`, copied and pasted into `text-a4.pdf` through the product, saved and reopened fresh, their bytes the label's SHA-256s (`tst_exchangeproduct`, `[I-21]`) -- what found **I-21**.
**Goal** D6-22.
**Steps** Copy two annotations; paste into another tab; paste into a second RawPage process; cut one; put an image on the clipboard and paste; then `Ctrl+V` and `Ctrl+X` in the find bar, a properties-bar text field and the tool finder.
**Expect** The pasted annotations equal in every modelled field but `/NM` and `/P`; the cut is copy plus one delete; the image becomes an image stamp; **every text widget keeps its own `Ctrl+V` and `Ctrl+X`** (Stage 5's I-04 turned round). **If it fails** S2.
**Result so far** *2026-10-09, T10* (`tests/ui/tst_stamps`, `[A-81]`, KEY EVENTS through the application): a 40 by 20 translucent image on the clipboard and `Ctrl+V` on the page -- `edit.paste`, §8's Common row, registered -- is ONE command, an image stamp (`/IT /StampImage`, its alpha kept); then, with text on the clipboard, `Ctrl+V` in the find bar's field, the properties bar's text field and the tool finder's box each pastes the text and makes NOTHING. **Bound:** offscreen, the clipboard is the process's own, not Windows'. **Owed (T13):** annotations copied and pasted into another tab and another process, the cut, and `Ctrl+X` in the text widgets -- `edit.cut` is not registered before T13. Planted P14 (`edit.paste` not registered) failed it.
**Result** ✅ *2026-10-16, T13.* **Through the widgets** (`tests/ui/tst_exchange`, `[A-81]`, KEY EVENTS through the application): a square and a note selected and `Ctrl+C` on the page -- nothing made; the clipboard holds RawPage's type `application/x-rawpage-annotations` and the words *"a square\na note"*; in ANOTHER TAB, `Ctrl+V` is ONE command adding THREE -- the square, the note and the note's pop-up -- each a new 32-digit `/NM`, every other modelled field equal to the copied one's, the note and its pop-up still naming each other, the two comments selected; the BYTES ALONE, as a second process would have them, pasted into a third tab the same (`MainWindow::pasteAnnotations`), and bytes that are not RawPage's comments refused by `error.clipboard.malformed`, nothing made. `Ctrl+X` with the square selected is ONE `DeleteAnnotationCommand` (`command.annot.delete`), the square gone and on the clipboard; with nothing selected `edit.cut` is DISABLED. **Every text widget keeps its own `Ctrl+X`**: in the find bar's field, the properties bar's text field (a note selected, its words the field's) and the tool finder's box, `Ctrl+X` cuts the field's selected words to the clipboard and the selected note is NOT cut -- the journal unmoved. **Through the product** (`tests/composition/tst_exchangeproduct`, `[A-81]`): a highlight, a square and a note copied from `annots-every-type.pdf` by `edit.copy` and pasted by `edit.paste` into `text-a4.pdf` -- one command -- SAVED and reopened fresh: each with every modelled field as copied but its identity, name and page, a new `/NM`, drawn by our builder. **Bound** (convention 19): offscreen the clipboard is the process's own; a second RawPage process is the bytes alone, and Windows' clipboard between two processes is not reached by any case here. Planted P16 (a paste keeping the copied `/NM`), P17 (a cut that only copies) and P18 (`Ctrl+C` copying the text selection over selected annotations) each failed it.

### 5.14 Attachments (T11)

**A-82 · The pane lists what the label lists** ✅ *2026-10-12, T11*
**Steps** `tst_attachments` over `attachments.pdf`.
**Expect** Every document-level and annotation attachment with its name, description and size equal to the label. **If it fails** S2.
**Result** ✅ *2026-10-12, T11.* **The reading** (`tests/unit/tst_attachments`, `[A-82]`, first-party through `CosBackend`): the three document files in the label's order, each key, name, `/Desc`, MIME type, size and `/ModDate` equal to the label's, a sixteen-byte MD5 `/CheckSum` that IS the MD5 of the decoded bytes, and no bytes in the model; the pushpin's file -- its name, description, type and size -- equal to the label's; the control file `text-a4.pdf` read, and holding none; a key the document does not hold refused by `error.attachment.unknown`. **The shipped backend** (`tests/render/tst_attachments`, `[A-82]`): the same against the label, and held to the ENGINE -- the attachment names PDFium enumerates for the search units (D5-12) are the model's, in its order. **The pane** (`tests/composition/tst_attachmentproduct`, `[A-85]`; `tests/ui/tst_attachments`): `Ctrl+Shift+A` brings it in front listing four rows -- the three files, then `measurements.csv` on page 1 -- each with its description, size, date and where it is. Planted P1 (`/Desc` not read) failed it.

**A-83 · Adding and deleting are commands** ✅ *2026-10-12, T11*
**Steps** Add a file; delete one; save; reopen; undo each.
**Expect** `AddAttachment` and `DeleteAttachment`; the saved file's `/EmbeddedFiles` as expected; each undo exact. **If it fails** S1 for a lost attachment.
**Result** ✅ *2026-10-12, T11.* **The commands** (`tests/unit/tst_attachments`, `[A-83]`, saved through `SaveCoordinator` and reopened fresh): on `attachments.pdf`, a 70 KB file attached and undone before a save -- the model hash restored and NOTHING to write -- then attached again, `setup.exe` deleted, and saved: the tree reads `added.bin, appendix.pdf, readme.txt`, the new file's description, size, type and MD5 as attached and its bytes the bytes; the delete UNDONE AFTER THE SAVE and saved again lists `setup.exe` back as the very pair the original file held -- the same reference to the same file specification (D6-38(a)); the attach undone after both saves is gone from the third. A document with none (`text-a4.pdf`) gains a tree and, losing its last file, loses it. A key held, a file with no bytes and a key not held are refused, changing nothing. A pushpin made with a file (`AddAnnotationCommand`) is saved with that file as its `/FS`, its bytes read back. **The save's verify** (`[verify]`): an attached file's stream changed by one byte after compose is refused by `error.save.verifyFailed`, *attachments*, and nothing is written; with no fault the same save is. **Through the pane and the tool** (`tests/composition/tst_attachmentproduct`, `[A-83]`): add, delete and its undo, each ONE command, the list following; the pushpin tool armed by `annot.attachFile`, a click on page 1 and the chosen file attached there -- the pin's point at the click to a point -- and all of it read back from the saved file; `tests/ui/tst_attachments`: a pushpin deleted from the pane goes with its pop-up, one command. **A-28 and A-29** re-run with the two commands: reversibility *2117 combinations over 100 editable corpus files; 0 failures*, and both round-trip through `CommandRegistry` (with fifteen hostile shapes refused by `error.command.malformed`). Planted P2 (a pair put back at the end), P3 (an attached file not written), P4 (verify blind to a changed stream), P12 (the pushpin carrying no file), P16 (an unsaved file reading as nothing) and P17 (a delete undone in the model only) each failed it.

**A-84 · Extract writes the bytes and nothing else** ✅ *2026-10-12, T11*
**Steps** Extract each attachment into a scratch folder; census the scratch, temp and `%LOCALAPPDATA%\RawPage\` trees before and after.
**Expect** Each file's SHA-256 equals the embedded stream's decoded bytes; **nothing else written** anywhere. **If it fails** S2.
**Result** ✅ *2026-10-12, T11* (`tests/render/tst_attachments`, `[A-84]`, the shipped backend): all four attachments -- the three files and the pushpin's -- extracted into a folder the extract makes, each file's SHA-256 the label's; a second extract to the same path refused by `error.attachment.exists` and the file unchanged, and replaced when asked; the scratch folder then holds exactly the four files (no `QSaveFile` temporary left); a key the document does not hold writes nothing. **Bound, stated:** other suites write into the temporary folder in parallel, so the temp and `%LOCALAPPDATA%\RawPage` censuses assert that no file NAMED like an attachment appeared or changed there, outside the scratch folder the extracts were asked for (which a `QTemporaryDir` puts under the temporary folder). The product's extract, where the person chose: `tst_attachmentproduct`, `[A-84]`. Planted P15 (an extract replacing a file unasked) failed it.

**A-85 · Opening is asked, and an executable is refused** ✅ *2026-10-12, T11*
**Goal** D6-17.
**Steps** Open the attached PDF; open the `.txt`; open the `.exe`-named file; quit cleanly; create a `FileAttachment` with `annot.attachFile`.
**Expect** The PDF opens in a tab **from bytes** (no file written); the `.txt` asks first and then lands in the session folder; the `.exe` is **refused** and nothing is written; the session folder is **empty after the quit**; the pushpin annotation is created. **If it fails** S1 if an executable is written or launched; S2 otherwise.
**Result** ✅ *2026-10-12, T11.* **Through the product** (`tests/composition/tst_attachmentproduct`, `[A-85]`; the session folder in a directory the case owns, the shell a recorder, the question answered through `MainWindow::setAttachmentAnswersForTesting`): `appendix.pdf` opens in a SECOND TAB from its bytes -- an attachment copy titled `appendix.pdf`, one page, NOT editable, no question asked, no file written anywhere and no session folder made; `setup.exe` is refused before anything is read -- no question, no folder, nothing to the shell; `readme.txt` ASKS -- answered no, nothing is written; answered yes, ONE file lands in `<session>/1/readme.txt`, its bytes the attachment's, and that path goes to the shell; opened again, a second folder; and when the product is destroyed as `main.cpp` ends -- the clean quit -- the session folder is GONE. The copy saved as a file of its own is reopened from it, editable, the file the attachment's bytes. The pushpin: A-83's tool case. **The backend half** (`tests/render/tst_attachments`, `[A-85]`): `openBytes` gives one page, no path, permissions refused by `error.edit.attachmentCopy`, a sink kept -- a Save As writes the attachment's bytes byte for byte -- and text bytes are refused by `error.open.notPdf`. **The policy hook** (`tests/unit/tst_attachments`): `isExecutableName` against 17 names it must refuse -- `SETUP.EXE`, `report.pdf.exe`, `setup.exe.` with Windows' ignored trailing dot -- and 9 it must not, and the label's own `executableNames`; `safeFileName` against separators, reserved characters, device names and length. **Bound** (convention 19): the dialogs a person sees and Windows handing a file to its program are M-13's. Planted P5 (no name executable), P6 (a non-PDF opened without asking), P7 (the folder not emptied at quit) and P14 (an attached PDF opened editable) each failed it -- **P7 only after the case was strengthened**: its session folder sat inside the product's own scratch directory, deleted WITH the product, so *emptied at the quit* held whether or not the quit emptied anything; the folder is now in a directory the case owns and outlives the product.

**A-86 · Large payloads go to side files, through the existing write** ✅ *2026-10-12, T11*
**Goal** D6-23.
**Steps** Add a 5 MB attachment and an image stamp; read the journal directory; `layer-lint --explain`.
**Expect** The journal line names each payload by SHA-256; the side files exist with those hashes; **15** waivers, unchanged. **If it fails** S2.
**Result** ✅ *2026-10-12, T11* (`tests/unit/tst_attachments`, `[A-86]`): a 5 MB attachment, an image stamp whose pixels are past 64 KiB, and a six-byte file through a recording journal -- the journal is under 64 KiB, holds `{"sideFile":"<hash>"}` for each long payload and the small file inline, and the directory holds exactly TWO `.side` files, each named by the SHA-256 of its content; replayed into the file as opened, three steps, the same model hash and the 5 MB file's bytes; a journal seeded from that replay carries both side files, and its discard takes them; a side file whose content no longer matches its name, and one deleted, each stop replay at its step by `error.recovery.sideFile`; `discardDirectory` removes them with the journal. `layer-lint --explain`: **15** escapes, the same fifteen -- the side files are written by `RecoveryJournal`'s existing whole-file write. Planted P8 (no payload sent to a side file) and P9 (a side file's hash not checked) each failed it.

### 5.15 Measurement (T12)

**A-87 · The arithmetic** ✅ *2026-10-14, T12*
**Steps** `tst_measure`: distances, perimeters and areas at several scales, units and precisions, against hand-computed values.
**Expect** Equal to the declared precision; a self-intersecting polygon's area refused by name rather than reported as a signed sum. **If it fails** S2.
**Result** ✅ *2026-10-14, T12* (`tests/unit/tst_measure`, `[A-87]`; every figure worked by hand in the case's comment -- 72 points an inch, 864 a foot, 25.4 mm an inch): the nine units exact in points and their symbols round-tripping as the settings' tokens; the scale in words -- 48 ft *"1 in = 4 ft"*, 120 ft *"1 in = 10 ft"*, 100 m *"1 cm = 1 m"*, 2.5 cm *"1 mm = 0.25 cm"*; at 1 in = 4 ft the corpus rectangle's side 216 pt **12.00 ft**, its diagonal 259.5997 pt **14.42 ft** (14.422 at three decimals, 14 at none, 173.07 in), three sides 504 pt **28.00 ft**, its area 31,104 sq pt **96.00 sq ft**, a concave L 23,328 sq pt **72.00 sq ft**; an inch at 1:100 **2.54 m**; ten inches at 1:100,000 **25,400.00 m** -- ISO's comma between thousands; a number format's own separators (*"1.234.567,9"*). **The figure eight**: `selfIntersects`, and `polygonArea`, `measureAt` and `newMeasurement` each refusing it by `error.measure.selfIntersecting` -- a fold back along one edge too -- with a square, the concave L and a square whose last corner repeats its first as the controls; too few corners and a corner at infinity refused by name. A measurement resized reads what it now spans (12.00 ft halved to **6.00 ft**) and its `/Contents`, which held the reading, follows -- a person's words do not (`core::withFreshReading`). Through the tool (`tests/ui/tst_measuretool`): a figure eight's area makes nothing, the journal unmoved, and the status bar says the catalog's words for the key. Planted P1 (the area summed, not refused), P2 (the scale string's paper unit always an inch), P3 (`/X` the ratio, not the ratio over the unit) and P22 (a resized measurement's `/Contents` left as it was) each failed it.

**A-88 · `/Measure` is written and read** ✅ *2026-10-14, T12*
**Steps** A distance, a perimeter and an area at a 1 in : 10 ft scale; save; read back; read `measure-scale.pdf`'s existing ones.
**Expect** `/IT` the dimension kind; `/Measure` `/Subtype /RL` with the scale string and conversion factors; the value read back equals the one shown. **If it fails** S2.
**Result** ✅ *2026-10-14, T12.* **Written** (`tst_measure`, `[A-88]`, through `AddAnnotationCommand` and the save path over `text-a4.pdf`): a distance, a dimension callout, a perimeter and an area at 1 in : 10 ft -- by hand 144 / 7.2 = **20.00 ft**, 216 / 7.2 = **30.00 ft**, (72 + 72) / 7.2 = **20.00 ft**, 72 x 72 / 7.2^2 = **100.00 sq ft** -- each saved with its `/IT`, `/Measure /Type /Measure /Subtype /RL /R (1 in = 10 ft)`, `/X [<< /U (ft) /C 0.13888… /D 100 >>]` (120 / 864, to 1e-12) and `/A`'s unit `sq ft`; read back first-party, its `/Measure` EQUAL to the one made and its reading the one shown; the area with `writeToComment` off has no `/Contents` and its appearance still sets *100.00 sq ft*. The model's JSON round-trips a measurement, and a label's one-format object reads as a chain of one. **The corpus's** (`[A-88]`): `measure-scale.pdf`'s three -- each `/Measure`'s `/R`, unit and factor the label's, and **what each measures, computed from its geometry and its own `/Measure`, the label's `/Contents`**: *12.00 ft*, *28.00 ft*, *96.00 sq ft*; recoloured, each is REDRAWN reading the same text in the new colour, its producer's `/Measure` untouched (D6-5). `tests/render/tst_annotreader`'s label comparison now covers `leaderLength` and `measure` over every fixture. **Through the product** (`tests/composition/tst_measureproduct`): two measurements made by the tool after calibration, saved with *Save*, reopened in a fresh engine document: the same `/R`, reading and `/Contents`. **A producer's measurement this build cannot display** -- a 100-character `/U`, `/C 0` -- is held (the unit cut at the 64 a command accepts, the format not plain), MOVED with its move's journal line read back, and its recolour refused by key. `tst_command_serialize`'s hostile case gains ten T12 rows. Planted P4 (the reader dropping `/Measure`), P5 (`/X` not written), P18 (a line's caption not drawn), P21 (a long unit kept whole) and P23 (an area's reading not drawn) each failed it.

**A-89 · Snapping lands on the generator's points** ✅ *2026-10-14, T12*
**Goal** Rule 6, D6-14.
**Steps** `tst_measuretool` over `measure-scale.pdf`: drags ending 3 device px from each known endpoint, midpoint and intersection; at 100 % and 400 %; `units/snapToObjects` off.
**Expect** Each snaps **exactly** to the generator's coordinate; the threshold is the same in device px at both zooms (Stage 4's rule); precedence point > edge > guide > grid; with the key off, no object snap. **If it fails** S2.
**Result** ✅ *2026-10-14, T12.* **Through the product, over the real file** (`tests/composition/tst_measureproduct`, `[A-89]`; the points read from `corpus/labels/annots-measure-scale.json`, the paths by the shipped engine): a distance started THREE DEVICE PIXELS right of each of the label's fourteen points -- *"A-89 at 100 %: 14 of 14 drags started 3 device px off landed exactly on the generator's point"*, and **14 of 14 at 400 %** -- the line's first point EQUAL to the generator's coordinate; with `units/snapToObjects` off the same aim lands where the pointer was. **Through the widgets** (`tests/ui/tst_measuretool`, the fake backend's paths): a drag from 3 px off a corner to 2-3 px off a midpoint, and from the diagonals' crossing to a corner, each line EQUAL to the coordinates at zoom 1 and 4; the hovered snap SAID before the press (a midpoint's kind and point); with the key off, the pointer's own point and NO request for the page's paths. **Headless** (`tests/unit/tst_snap`, `[A-89]`): the corpus drawing's endpoints, midpoints and a built crossing exact from 3 device px at 1.389 and 5.556 px a point; a hundredth of a pixel inside the threshold snaps and one pixel past it does not, at 50 %, 100 % and 400 %; a point on a path only where no corner, midpoint or crossing is in reach (and only with `measuring/snapToPaths`); the precedence -- an annotation's side nearer on x loses to the corner, with points off the edge takes x, with neither object kind nothing of the drawing; the grid index finding every segment a scan finds. **The engine's reading** (`tests/render/tst_measuregeometry`, `[A-89]`): page 1's paths are EXACTLY the generator's eight segments, each label point a target at a hundredth of a point, page 2 none; a form's line through its `2 0 0 2 50 50` matrix at (50, 50)-(250, 50), a Bezier's eight pieces and its two anchors, a clipping path not read. **Found by this case's curve: I-19** (§8). Planted P6 (point targets ignored), P7 (no midpoint), P8 (`units/snapToObjects` ignored), P9 (the threshold in page units), P12 (forms not walked), P13 (an object's matrix not applied), P14 (I-19 put back) and P24 (the tool's points not snapped) each failed it.

**A-90 · Calibration reads true** ✅ *2026-10-14, T12*
**Steps** Measure the drawing's 100 mm line, calibrate it as 100 mm; measure the 37.5 mm line.
**Expect** `measuring/scaleRatio` set; the second reads 37.5 mm at the set precision. **If it fails** S2.
**Result** ✅ *2026-10-14, T12* -- **a deviation from the steps**: `measure-scale.pdf` has no 100 mm and no 37.5 mm line, so the case calibrates one length the generator drew against others it drew, which is the same claim. **Through the product** (`tests/composition/tst_measureproduct`, `[A-90]`): `annot.measureCalibrate`, the rectangle's 216-point side measured end to end (snapped), the dialog answered *12 ft* -- **`measuring/scaleRatio` 48** (12 x 864 / 216), the unit feet, NOTHING made; then the 288-point line reads **16.00 ft** and the diagonal **14.42 ft**, and both are saved as shown. **Through the widgets** (`tst_measuretool`): the same, the status bar saying *1 in = 4 ft*, and the next tool armed no longer calibrating. **Headless** (`tst_measure`, `[A-90]`): `calibratedRatio(216, 12 ft)` is 48 to 1e-12, and nothing measured, a negative length and a ratio past the key's range are refused by `error.measure.calibration`. **The dialog** (`tst_measuretool`): 1 in standing for 4 ft is 48 with *"1 in = 4 ft"* shown; a length of zero states no scale and OK is disabled; `annot.measurementScale` writes the three keys (1 cm : 1 m is 100), a cancel writes nothing, and the command is DISABLED where nobody can be asked (convention 16). Planted P15 (calibration making a measurement instead) failed it.

**A-91 · Path geometry is engine work, bounded and kept** ✅ *2026-10-14, T12*
**Steps** `tst_measuretool`: thread ids for `pageGeometry`; a page of 150,000 segments (synthetic); a second request for the same page.
**Expect** Read on the render thread; the large page gives no object snaps and says so **once**; the second request served from `DocumentIndex` with no engine job. **If it fails** S2.
**Result** ✅ *2026-10-14, T12* -- the steps spread over three suites, each the one that can see its clause. **The engine** (`tests/render/tst_measuregeometry`, `[A-91]`): `pageGeometry` through the shipped `RenderScheduler` and `DocumentIndex` as `app/CompositionRoot` wires them -- the reader ran on ONE thread that is not the asking one, ONE engine task (`taskEnqueued` 1), eight segments; **the second request asked nothing** (no task, no call); a built page of **150,000** segments is answered `tooManySegments` -- the walk STOPPED at the 100,001st segment, nothing held, no target. **The index** (`tests/unit/tst_index`, `[A-91]`): one read per page on the engine thread, `GeometryArrived` told, the second request served (`geometryRequestedFor` 1), an annotation's scope keeping the paths and a content scope dropping them, a page the reader cannot read stored EMPTY and not asked again, and a `window.new` companion's handle reading nothing twice. **Said once** (`tests/ui/tst_measuretool`): five aims over a page of 100,001 segments -- `tooManySegments` emitted ONCE, `pageGeometry` called once, no object snap, and the status bar naming the count. Planted P10a (the bound not applied by the model), P11 (the paths not kept) and P20 (the dense page said on every aim) each failed it -- and **P10b (the bound not applied by the ENGINE'S READ) STAYED GREEN first**: the model's own bound still made the page `tooManySegments`, so *nothing held* was true whether or not the walk stopped; the case now asserts the walk stopped at the bound (`counted()` exactly 100,001 -- the read's time and memory are what the bound is for), and P10b fails it.

**A-92 · The keys and the page, and the comment** ✅ *2026-10-14, T12*
**Steps** The Measuring page; `measuring/writeToComment` on, then off; a dimension callout.
**Expect** Five keys with generated editors and `units/snapToObjects` on Units & Guides; the readout in `/Contents` exactly when on; the callout's leader lines (`/LL`, `/LLE`). **If it fails** S3.
**Result** ✅ *2026-10-14, T12.* **The keys** (`tests/unit/tst_stage6settings`, `[A-92]`): the Measuring group holds exactly the five `measuring/*` rows and `units/snapToObjects` is in Units & Guides; their defaults (1, inches, 2, on, on; objects on); every unit written and read back by its symbol, *ft* in the file, a hand-edited *furlong* falling back to the default, a precision of 9 clamped to 6. **The page** (`tests/ui/tst_viewsettings`): **twelve** of 26 pages ship, Measuring among them, built from the table -- one generated editor per key, the display unit a choice of the nine (each `prefs.choice.measureUnit.*` resolving in both locales). **The comment** (`tst_measuretool`): with `writeToComment` off the measurement's `/Contents` is empty and it still measures; on, it holds the reading; and the file half (`tst_measure`) as above. **The callout** (`tst_measuretool`): `annot.dimension` -- two clicks and a third 30 points below a left-to-right line -- one Line whose `/LL` is the offset along the clockwise normal (positive), `/LLE` 3, `/Cap` on top; saved, `/LL` and `/LLE` written and a plain distance writing neither (`tst_measure`); its appearance (`golden/appearance/measure-distance`) LOOKED AT before it was kept. Planted P16 (the reading written whatever the key) and P17 (the leader on the wrong side) each failed it.

### 5.16 Exchange (T13)

**A-93 · XFDF and FDF round-trip every field** ✅ *2026-10-16, T13*
**Goal** §3.7.
**Steps** `tst_fdf` over every fixture.
**Expect** §3.7's equality for both formats. **If it fails** S1 for a lost annotation; S2 for a field.
**Result** `unit.every comment the labelled corpus holds round-trips through XFDF and FDF in every modelled field` (`tst_fdf`): over the eleven annotated corpus files, each document's comments exported by `AnnotationStore::exportFdf`, written by `XfdfCodec::write` / `FdfCodec::write`, read back, merged into the same file with its comments STRIPPED, the result SAVED and reopened -- *"A-93 XFDF: 123 comment(s) exported over 11 file(s); 121 imported equal in every modelled field, 2 refused as this build cannot write them"*, and the same for FDF. Equal is §3.7's equality field by field -- subtype, rectangle, every geometry, colour, interior colour, opacity, border, dash, cloud, blend, line endings, the text fields, both dates, flags, `/IRT` and `/RT` by NAME, state, a stamp's name and pixels, a file attachment's file byte for byte, `/Measure` -- every difference a `CHECK` naming its field; the two refused are a redaction mark and a sound (T14's kinds), each by `error.pdf.appearanceUnsupported`. **Corrected at T14 (2026-10-16), when the case was made to NAME each refusal in its output:** the T13 figure's two refused were the redaction mark and `annots-mixed.pdf`'s free text -- a `/DA` face this build does not draw, refused by key since T7 (D6-34(g)) -- while the SOUND was left out of the export altogether (*not exported*, T13's note), so it was among neither count. **Since T14:** *"A-93 XFDF: 124 comment(s) exported over 11 file(s); 123 imported equal in every modelled field, 1 refused"* and *"refused: ordinary/annots-mixed.pdf FreeText (error.pdf.appearanceUnsupported FreeText)"*, the same for FDF -- the sound exported, the mark and the sound imported equal. `unit.an XFDF RawPage writes says every colour exactly and XFDF's own form for every other reader`: an eight-bit `#RRGGBB` beside the `rp:` exact components only where the hex is not exact, a carriage return as `&#13;`, a callout's one ending, a rich-text body as elements. Planted P5 (the hex alone), P6 (a carriage return bare), P7 (an FDF's `/Page` ignored -- only after A-94 compared each comment's page) and P22 (a callout read with two endings) each failed it.

**A-94 · Acrobat-shaped exports import** ✅ *2026-10-16, T13*
**Steps** Import each `corpus/exchange/` file into its target.
**Expect** Every annotation present with the label's fields; the one `ImportAnnotations` in the journal. **If it fails** S2.
**Result** `unit.the Acrobat-shaped exports import with every field the label holds` (`tst_fdf`): each of `corpus/exchange/`'s four Acrobat-shaped files imported into its target with its comments stripped -- the journal's last step ONE `command.annot.import` -- saved, reopened and held to the LABEL, field by field and by the page it is on: *"every-type.xfdf: 18 of the label's comments imported with every field, 2 refused (a kind T14 writes)"*, the same for `every-type.fdf`, and 21 of 21 for `threads.xfdf` and `threads.fdf` (`threads`' comments on both its pages, each checked ON the page its label lists it under). A reply whose parent the file does not hold arrives as a comment of its own, said to (`ReferenceDropped`). **Through the product** (`tst_exchangeproduct`, `[A-94]`): `threads.xfdf` imported by `MainWindow::importCommentsFrom` into `annots-threads.pdf` -- *"21 comments imported and saved; live against the file reopened fresh: 0 differing bytes on two pages at 72 and 144 dpi"* -- and `every-type.xfdf` into `report-v2.pdf`, eighteen as one command and the two refused by key and NAMED in the report. **Through the widgets** (`tst_exchange`): `comment.import` is one command, its report modeless, a refusal in its rows. Planted P3 (a reference to nothing kept) and P21 (the shipped backend answering every kind writable) each failed it. **Since T14 (2026-10-16):** *"every-type.xfdf: 20 of the label's comments imported with every field, 0 refused"*, the same for `every-type.fdf` -- the redaction mark and the sound with them -- and any refusal of these files now FAILS the case; through the product, `every-type.xfdf` imports all twenty as one command, the mark's overlay text and repeat and the sound's samples (the label's SHA-256) read back from the saved file. T13's refusal-and-report clause is kept by a scratch copy of `every-type.xfdf` whose note's icon is `Key` and pushpin's `Graph` -- two icons the builder refuses by name -- eighteen imported and those two refused and named.

**A-95 · Merge by `/NM`** ✅ *2026-10-16, T13*
**Steps** Import an XFDF holding one new `/NM`, one known with a newer `/M`, one known with an older `/M`; undo.
**Expect** Added, replaced, kept — and the `MergeResult` says each; one undo restores all three. **If it fails** S2.
**Result** `unit.a merge adds the new replaces the newer keeps the older and one undo restores all three` (`tst_fdf`): `annots-threads.pdf`'s own export with one new name, one known name a day NEWER (`/M`) with new words and one a day OLDER -- *Added*, *Replaced*, *Kept*, each in the `MergeResult`; the replaced one keeps the document's identity, page and pop-up; one `Ctrl+Z` restores the model hash and an EMPTY sink delta, and redo brings all three back. `unit.a merge refuses one comment by key and keeps the rest`: over `annots-flags.pdf` each comment comes back moved a point with new words -- `Locked` refused by `error.annotation.locked` (D6-27) and its unlocked twin replaced; a name twice in one file and a kind not written refused each on its own; the rest one command. `unit.a document's own export merged back changes nothing, a name or none`: every comment *Kept*, no command -- and over `annots-mixed.pdf`, whose comments carry no `/NM`, each named by what it says, the same name both ways. Planted P1 (every known comment newer), P2 (the lock not asked) and P9 (the import's undo forgetting what it replaced) each failed it.

**A-96 · Export only the filtered** ✅ *2026-10-16, T13*
**Steps** Filter the pane by author; export.
**Expect** Exactly the filtered set (with their threads) in the file, written by `pdf/ops`. **If it fails** S2.
**Result** `unit.an export of a filtered list carries exactly those comments and their threads` (`tst_fdf`): one author's comments of `annots-threads.pdf` exported -- exactly them, with every reply and status of their threads by whoever wrote it, and no comment of anyone else's that is not in them. `unit.an exchange file is written through pdf/ops and an existing one is replaced only when asked`: `FdfCodec::writeFile` through `AttachmentFiles::write` -- no waiver, the census still fifteen -- a file there refused by `error.exchange.exists` unless asked to replace. **Through the widgets** (`tst_exchange`): `comment.export` with the Comments pane filtered by author writes what the pane LISTS (A-67's filter), no command. **Through the product** (`tst_exchangeproduct`): the same, read back from the file written. Planted P8 (an FDF without a file attachment's file), P15 (the pane's filter ignored) and P19 (an export without the threads) each failed it.

**A-97 · A hostile XFDF is refused** ✅ *2026-10-16, T13*
**Steps** Import the external-entity fixture, a billion-laughs fixture and a truncated file.
**Expect** **No file or URL resolved** (a scratch file the entity names stays unread); bounded time and memory; each refused by key, the document unchanged. **If it fails** S1.
**Result** `unit.an XFDF with a document type declaration is refused by key before any entity exists` (`tst_fdf`): an internal entity, an EXTERNAL entity naming a scratch file that holds a secret (`file:///...`) and a billion-laughs file each refused `error.exchange.dtd` -- the secret in no result -- *"A-97 billion laughs refused in 0 ms"*; the control, the same file with no declaration and the words literal, reads; an entity never declared is `error.exchange.malformed`. `unit.a truncated malformed or foreign exchange file is refused by key and a huge one read bounded`: a cut XFDF `error.exchange.malformed`, a PDF and plain text `error.exchange.notExchange`; an FDF cut at every length read as far as it goes or refused, never a crash; a million-point ink cut at `kMaxExchangeCoordinates` and said to be; a 50,000-deep rich-text body read in under 5 s. The document is never touched -- a refused read reaches no merge. Planted P4 (a declaration not refused) failed it. **Bound** (convention 19): XFDF's reading is `QXmlStreamReader`'s, which resolves no external entity in any Qt; the refusal is what makes that a rule here rather than a property of the library.

**A-98 · Migration, graded** ✅ *2026-10-16, T13*
**Goal** §3.5.
**Steps** `tst_migrate` (headless, over the two documents' text and Stage 5's alignment) and its composition half.
**Expect** Every §3.5 bar met. **If it fails** S2.
**Result** `render.comments migrate to the revision on the guide's bars and what cannot is reported and not placed` (`tst_migratecorpus`, over the engine's text and Stage 5's alignment): `annots-report-v1.pdf`'s comments to `report-v2.pdf` -- *7 placed, 7 of them where the label says (precision 100 %); 7 of 7 placeable placed (recall 100 %)*, each placed markup's quadrilaterals within the bar of the label's on the revised page. `unit.` (`tst_migrate`, seven cases over synthetic pages): markup moves with its words to the page and place they now have; each quadrilateral of a phrase across two lines moves with ITS OWN first glyph when the lines moved differently; a phrase held twice goes to the occurrence NEAREST where it was and never into a longer word; a note keeps its place beside the nearest line of a text block -- never a table's -- with its reply and pop-up. **Through the product** (`tst_exchangeproduct`): `comment.migrate` from `annots-report-v1.pdf` into a tab of `report-v2.pdf`, SAVED and graded from the file -- *"7 of 7 placeable comments where the label says"*. Planted P11 and P11b (a whole word into the end or the start of a longer one -- each only after the case gave each edge a word of its own), P12 (the first occurrence, not the nearest), P13 (a table's line as an anchor -- only after a case put a table beside a note) and P23 (every quadrilateral moved by the phrase's first glyph) each failed it.

**A-99 · What cannot be migrated is reported and not placed** ✅ *2026-10-16, T13*
**Steps** The annotations on text `report-v2` deleted.
**Expect** Each in the report; none in the revised document. **If it fails** S1 — a silent misplacement in someone else's revision.
**Result** `unit.markup on words the revision deleted is reported and nothing of it placed` and `unit.a note whose line changed and a comment on a deleted page are reported and never placed` (`tst_migrate`): each REPORTED by key -- `error.migrate.textGone`, `anchorGone`, `pageGone` -- and in no page of the result. Over the corpus (`tst_migratecorpus`) and through the product (`tst_exchangeproduct`): *"A-99: 3 of 3 reported and in no page of the file"*, each named with its reason in the report dialog's rows. Planted P14 (a comment that cannot be placed placed anyway) failed it.

### 5.17 Redaction marks and audio (T14)

**A-100 · A mark removes nothing** ✅ *2026-10-16, T14*
**Goal** D6-25.
**Steps** `Ctrl+Alt+K` over an area and over a text selection, with overlay text, colour and repeat set; save.
**Expect** `Redact` annotations with `/QuadPoints` (text), `/OverlayText`, `/IC`, `/Repeat`, `/AP /N` and `/RO`; **every content stream of every page byte-identical** to the original's; the text still extractable and found. **If it fails** S1.
**Result** **Through the tool** (`tst_redactink`, `[A-100]`): on `text-a4.pdf`, `Ctrl+Alt+K` from the page with nothing armed, the overlay text `REDACTED`, the repeat and a blue outline set on the PROPERTIES BAR with the field focused, a drag across a 20-character phrase and a drag over the margin beside it -- two commands, one mark over the phrase's quadrilaterals and one over the area, each with the overlay, the repeat, the black fill and the person's name; saved; and the saved file held to three things: *"A-100 through the tool: 2 marks, 3 page(s) of content byte-identical, the phrase "RawPage corpus - tex" still extracted; A-34: 0 differing bytes live against the file"* -- each page's `/Contents` as its dictionary writes it and every byte of every stream it names, read first-party from both files; the phrase extracted by a FRESH engine from the saved file; the live page and the file reopened fresh 0 bytes apart at 72 and 144 dpi. *Found* is the phrase in the engine's extraction -- the find bar searches that text (D5-12), and is not driven here. Text already selected and `redact.mark` dispatched marks it at once, one mark a page the selection crosses and ONE command, nothing left armed; a selected mark's overlay text and repeat changed from the bar are one command each, and a selection that is not all marks greys both. **Headless** (`tst_redaction`): a text mark and an area mark saved through the sink -- every page's content BYTE-IDENTICAL, the original a byte-identical prefix, `/OverlayText`, `/IC`, `/Repeat` (written only when true), `/QuadPoints` and both forms read back well-formed; the corpus's mark read as its label says and RECOLOURED -- its outline redrawn, its `/RO` drawn anew with its overlay, every content stream still identical; an overlay edited as one command, its `/RO` redrawn and `/DA` turned black on a white fill; a Greek overlay refused by name before any command; both exchange formats round-trip the overlay and the repeat. Two appearance baselines, `redaction-marks` (the outlines) and `redaction-overlay` (each mark's `/RO` drawn in `/N`'s place: REDACTED once in white on black, WITHHELD tiled black on white, clipped), each LOOKED AT before it was kept. Planted P1, P3, P4, P5, P12, P13, P14, P15 and P16 each failed it.

**A-101 · A sound's samples round-trip exactly** ✅ *2026-10-16, T14 -- but the by-hand recording run, owed* *(Q5)*
**Steps** `tst_sounddata`: WAV files of 8 and 16 bit, mono and stereo, into a `Sound` annotation; save; read back; a by-hand recording run on the `windows` platform (skips offscreen, like Stage 5's A-50).
**Expect** `/R`, `/C`, `/B`, `/E` as the WAV's; the samples **byte-identical**; a non-PCM WAV refused by name; the recording run produces a `Sound` annotation that plays. **If it fails** S2.
**Result** **Headless** (`tst_sounddata`): WAV files built byte by byte -- 8-bit mono and stereo, 16-bit mono and stereo, each with and without an extra `LIST` chunk -- read as `/R`, `/C`, `/B` and `/E` (`Raw` for 8 bits, `Signed` for 16), each 16-bit pair turned big-endian, and written back to a WAV whose `data` chunk is the input's BYTE FOR BYTE; a sound saved as a `/Sound` stream by the sink and read back from the saved file -- its four numbers first-party, its samples through a fresh document's `soundSamples` -- and the WAV made from them holding the input's samples exactly; the corpus's tone read as its label says (rate, channels, depth, encoding, and the SHA-256 of its 4,000 bytes); an export in XFDF and in FDF carrying the sound's samples, read back equal to the label's hash; float, A-law, mu-law, MP3 and ADPCM WAVs refused by their tags (`error.sound.notPcm`, `0x0003` …), EXTENSIBLE judged by its subformat, three channels, 24 bits, a 500 Hz rate, a truncated chunk and part-frames refused by key; a hostile journal line's rate, depth or encoding refused by the command's parser. **Through the product** (`tst_audioproduct`): `annot.attachAudio` armed by its command, a click, the WAV answered through the window's hook -- ONE command -- and `annot.recordAudio` the same with a recording answered; a DOUBLE CLICK plays the attached sound, the player handed its samples; saved, a fresh engine's document gives both back byte for byte; the corpus's sound PLAYED from its stream, its bytes the label's; a float WAV refused, the status bar naming `0x0003`, nothing made; captured float four-channel audio mixed to one 16-bit channel exactly; and every `QByteArray` compared as hex or a SHA-256 (CLAUDE.md's reporter trap -- P2 first read as *no case ran* because of it). **OWED: the by-hand recording run** -- `ui.composition.a real recording through the recorder dialog` (`[A-101-record]`) SKIPS offscreen, as Stage 5's A-50 does; on the `windows` platform with a microphone it records about a second from the default input and PLAYS IT ALOUD, so it is run with the user's leave. Planted P2, P6, P7, P10, P11 and P17 each failed it.

### 5.18 The stage-wide sweeps (T15)

**A-102 · Every earlier case is still here, on both presets** ✅ *2026-10-20, T15*
**Steps** `ctest --preset x64-release` and `--preset x64-debug` after clean builds; the case names compared with Stage 5's sign-off run.
**Expect** Green on both; every Stage 0–5 case present **by name** or its rename said. **If it fails** S1.
**Result** After A-01's clean builds: **`ctest --preset x64-release` 1816/1816 and `ctest --preset x64-debug` 1816/1816**, the same **five named skips** on each -- the battery case (`platform.powersession`, no battery), Stage 4's A-85 `[I-14]` and A-94 `[I-23]` (a real desktop), Stage 5's A-50 `[D5-10]` (the system clipboard) and this guide's A-101 `[A-101-record]` (a microphone) -- and **`ctest --preset gates` 137/137**. **The names:** Stage 5's sign-off list was never taken (Stage 6 T1 took none), so T15 took it from the sign-off tree itself -- `b49b13c` built in a worktree of its own, `ctest -N` over its `x64-release` build: **1,422 names**, the figure Stage 5 signed off on, kept as `tests/baselines/stage5-signoff-test-names.txt`. Against `ctest -N` now: **1,418 registered unchanged and 4 RENAMED, none removed** -- `unit.all twelve Write-mode chords resolve to their word-processor meaning` and `unit.the census reports exactly twelve Write-mode collisions, split 8 + 4 + 0` are now *thirteen* and *9 + 4 + 0* (Q1, `a560c8e`, named in the plan's §10); `ui.the sidebar now lists exactly three panes, and still no more` and `ui.the seven unbuilt pane chords are NotImplemented and leave the key alone` are now *five panes* and *five unbuilt pane chords*, renamed at T9 (`91e0364`, the Comments pane: four and six) and T11 (`3194c8c`, the Attachments pane) -- T9's row recorded them as census edits and not as renames, so the Stage 6 status block's T15 row says it. Stage 4's list (`stage4-signoff-test-names.txt`): the same four and Stage 5's one recorded rename, nothing else.

**A-103 · No existing golden baseline moved** ✅ *2026-10-20, T15*
**Steps** `ctest -R "render.golden"`; `git diff b49b13c..HEAD -- golden/`.
**Expect** Green; only new baselines added, each with a reason in `golden/UPDATES.md`. **If it fails** S1.
**Result** `git diff --name-status b49b13c..HEAD -- golden/`: **99 files added and none modified** but four metadata files -- `captured.json`, whose **315 earlier entries are identical** entry by entry (414 now; only `capturedUtc` and `reason` moved at the top), `manifest.json`, with **no line removed**, `README.md` and `UPDATES.md`, which gives each capture's reason (T1's annotation fixtures, T8's, T10's and T14's appearance baselines). Every `render.golden.*` case passes in the `x64-release` run of 1816 (A-102).

**A-104 · A2 re-read through the shipped surfaces** ✅ *2026-10-20, T15 -- a VERDICT re-read: still outside §3.1, the verdict kept `go, with named custom work` by the user's choice, the rest deferred as `DW-4`*
**Goal** §3.1, the product half.
**Steps** `rawpage_bench_surface.exe --stage6`: the shipped composition with an ink tool drawing on `pages-900.pdf`.
**Expect** The figure printed with the dpi and viewport reached; graded on §3.1 and recorded beside T4's. **If it fails** S2.
**Result -- OUTSIDE §3.1, recorded; the user chose on 2026-10-20 to keep the verdict and defer what remains (`DW-4`, `Deferred-Work.md`).** `rawpage_bench_surface --stage6` (`tests/bench/bench_surface.cpp`'s Stage 6 columns): the product's composition, offscreen, printing *"288.0 dpi at zoom 288 % (device scale 1.50)"* and *"the drawing pane 1920x540 device px over the scrolling pane 1920x540 -- ONE window divided"* (a split divides one window, Stage 4 guide A-72's own correction, so the two panes together are A2's viewport); the top pane on page 450 of `pages-900.pdf`, **the shipped pencil** armed by `annot.pencil` and kept by `annot.keepToolSelected`, a press, 199 moves and a release every 100 ms for 30 s; the bottom pane, its scroll not synchronized, a screen every 400 ms (D4-16's pace). **Nine runs in three sets**: the scrolling pane's Visible p95 **21.9–29.4 ms** against **1.38–1.61 ms** with nothing drawn (both panes 36.8–41.6); **edit-to-pixels p95 201–505 ms, max 0.87–4.2 s**; the release -- the stroke made and its command run, compose and verify inside it -- p95 **88–143 ms**; 300 strokes and 300 commands a run. The first set read faster (edit-to-pixels p95 201–202, release 88–90) than the other two (357–505, 135–143), and the machine moved, not the code: a `bench_pipeline --stage6` run beside the last set read its viewer-alone column -- no annotation code in it -- 20–28 % slower than the first session. **T4's bench re-read beside it** with §3.1's own edit, an ink stroke of 200 samples (A-38's deviation gone), and the path geometry loaded: viewport p95 107–111 ms (129–273 in the second session), edit-to-pixels p95 307–414 ms, `journal.run` p95 90–133 ms, the reload 2.2–3.0 ms, a tile of the edited page p95 50–70 ms, a page's path geometry 0.11–0.30 ms. **The causes are T4's two and no third** -- a tile of a page carrying ~300 annotations costs ~50 ms by itself, and the show step's compose and verify grow with the unsaved annotations on the UI thread. Recorded in `Tech-Stack-Plan.md` §3.3 beside T4's figure. **Two defects of the bench, found and fixed in the task:** its first set read three strokes' rectangles from a page layout the release had already rebuilt (a stale `PageSlot*`), reported them as edits never shown, and was corrected to copy the slot first; and planted **P7** -- the pencil disarmed after each stroke -- STAYED GREEN over *"strokes 40, commands 1"*, because the column's self-check compared commands with inks and not with strokes; strengthened, P7 fails `bench.surface.a2_stage6`, the Stage 6 columns' short CTest registration. **Not covered:** a person's rate of drawing (M-12), a monitor at 60 Hz, one rig, our own corpus.

**A-105 · The action census agrees with itself** ✅ *2026-10-20, T15*
**Steps** `rawpage.exe --list-actions`; `tst_viewercommands`.
**Expect** The declared, registered and unregistered counts re-derived and equal to what the documents state; **`Ctrl+B` the only Viewer chord still `NotFound`**. **If it fails** S3.
**Result** `rawpage.exe --list-actions`: *"389 declared, 173 registered, 216 unregistered"* -- T14's figures, which `CLAUDE.md`, `Shipped-Surface.md` §2 and the T14 row state; T15 mints and registers nothing. `ui.the rest of the Viewer table is still NotImplemented and leaves the key alone` (`tst_viewercommands`) green: **52 Viewer rows registered and 1 not -- `Ctrl+B`**, `NotImplemented` and unconsumed, the only Viewer chord still `NotFound`.

**A-106 · The UI binary runs whole** ✅ *2026-10-20, T15*
**Steps** `rawpage_ui_tests.exe` whole, in declaration order and under three seeds, on both presets.
**Expect** Green each time; `ProcessStateGuard` silent (Stage 4's A-103). **If it fails** S2.
**Result** `rawpage_ui_tests.exe` whole under `QT_QPA_PLATFORM=offscreen`, on **both presets**, in declaration order (`--order decl`) and under three seeds (`--order rand --rng-seed` 101, 20261020 and 3735928559): **eight runs, each *"test cases: 497 | 494 passed | 3 skipped"*** (the three real-desktop cases), 35–39 s each, no `ProcessStateGuard` line in any. And `rawpage_composition_tests.exe` whole on both presets: **85 cases, 84 passed, 1 skipped** (the recording, A-101), seeds 505158432 (release) and 2079153116 (debug).

**A-107 · A killed session recovers its annotations** ✅ *2026-10-20, T15*
**Steps** `platform.recovery.kill` extended: `TerminateProcess` ×20 during annotation commands and saves, a large payload among them.
**Expect** Each recovery offer replays to a store equal to the journal's last line; side files present; no recovery offered for a session that ended cleanly. **If it fails** S1.
**Result** Three cases. **During commands** -- `platform.recovery.kill_annotations` (`tests/platform/tst_recoverykill`, `[A-107]`): `rawpage_killprobe annotations` runs the cycle of `tests/platform/killprobe/AnnotationSteps.h` -- an ink, a highlight, a move, a delete and a **200 KiB attachment, past D6-23's threshold and so a SIDE FILE** -- and is `TerminateProcess`ed twenty times at random instants off the timer tick; every journal it leaves is offered, replayed, and its model compared by `Document::modelHash` with **the same steps run again in the harness** (the cycle is deterministic by construction): every acknowledged step and at most the one in progress replayed, the model EQUAL to the harness's, the side files one per payload replayed (one more only when the kill fell between a side file and the line naming it), and each payload's bytes back by SHA-256. One run: *"between apply and the flush: 9; inside the flush: 11; runs replaying a side file: 15; most steps replayed: 10"* -- a run that reached neither window, or replayed no side file, fails as unmeasured. **During saves** -- `platform.recovery.kill_annotsaves`: `annotsaves`, one step then a save through `SaveCoordinator` and the real `WinDurableFiles`, killed twenty times: the target always EXISTS and is a whole revision (the last acknowledged save's hash, or the replacing file's), and that revision READ BACK holds, by name, exactly the kill-* annotations and the attachments the same steps leave -- *"between saves: 3; between writing the temporary file and the replace: 10; inside the replace: 2; writing the temporary file: 5; most saves acknowledged: 13"*. **A session that ended cleanly** -- `ui.composition.a session whose annotations end in a save or a discard leaves nothing to offer` (`tst_recoveryproduct`): two documents annotated by the same cycle through the product's own journals, a side file each, the window closed with SAVE for one and DON'T SAVE for the other (the dialog's twin, convention 16): both directories gone, `discover()` empty, the saved file changed and the discarded one byte-identical. **Planted, each failed its case:** **P1** every payload inline (`sides >= payloads`), **P2** a replayed move losing its `dx` (the model hash), **P3** a discarded document keeping its directory, **P8b** the reader losing every `/NM` (the saved revision's names). **P8** -- an ink written with no `/NM` -- also failed `kill_annotsaves`, but on its floor, not its names: Save's verify refuses a dictionary with no `/NM`, so the probe saved nothing and no kill reached a replace; P8b replaced it. **Not covered** (convention 19): the probe is not `rawpage.exe` and its replay is `tests::CosBackend`'s; the product's offer of a killed journal is Stage 3 guide A-74's.

**A-108 · Shown is saved, for everything the tools make** ✅ *2026-10-20, T15*
**Steps** A-34 re-run over every type T5 … T14 added, each created through its tool.
**Expect** **0 differing pixels** everywhere. **If it fails** S1.
**Result** **The sweep is a set of cases, one per tool family, and T15 wrote the one that was missing.** Each earlier task compared its kinds through its tools, live against the file reopened fresh in three views (72 dpi, 144 dpi, turned 90°): T6's markup and notes (`tst_markupink`), T7's typed kinds (`tst_typedink`), T8's shapes and the pencil (`tst_drawink`), T10's stamps (`tst_stampink`), T12's measurements (`tst_measureproduct`), T13's import (`tst_exchangeproduct`) and T14's marks (`tst_redactink`). **No suite had compared an ERASE (T8), a PASTE (T13), the PUSHPIN (T11) or a SOUND (T14).** `ui.composition.an erase a paste a pushpin and a sound made with their tools are shown as they are saved` (`tests/composition/tst_shownsaved`, `[A-108]`): on `text-a4.pdf`, a pencil stroke; the eraser dragged through it (one `EditAnnotation`, the stroke split in two); the stroke selected, `edit.copy` and `edit.paste`; `annot.attachFile` and a click; `annot.attachAudio` and a click -- five commands, the files answered by the window's twins -- saved: *"0 differing bytes over three views"*, every view different from the original, and all five undone after the save *"0 bytes from the original"* (A-35). Planted **P4** (the reload never swapped in) failed it. **Not covered:** the recording tool, which makes the same `Sound` (A-101).

---

## 6. Manual verification — you run this

Thirteen cases, **each on a copy of a generated corpus file** in `tests\manual\<case>\`, each asking for
a judgement no assertion makes. **Walk each at the task that makes it walkable**, not at the end:

| Walk at | Cases |
|---|---|
| **T6** | M-01, M-03, M-11 |
| **T7** | M-06 |
| **T8** | M-02, M-12 |
| **T9** | M-05 |
| **T10** | M-07 |
| **T11** | M-13 |
| **T12** | M-09 |
| **T13** | M-04, M-10 |
| **T14** | M-08 |

Three things about the state you will be walking, so a planned absence is not reported as a defect:

- **Every change you make is undoable and changes the file only when you save.** A file whose
  *modified* time moves without a Save is a finding.
- **There is no comment summary, no redaction applied, and no typed comment outside Latin and CJK**
  (Q3 and Q4, approved 2026-10-04).
- **Walk on copies in `tests\manual\<case>\`**, never on the corpus files themselves. **Set
  `identity/author` first**, so the comments are yours.

---

**M-01 · Marking up and noting a page** ☐
- **Goal** The markup tools and notes judged by a person: on the glyphs, the colour expected, the pop-ups where they should be.
- **Pre** T6. Copies of `columns-two.pdf`, `annots-rotated.pdf` and `cjk-vertical.pdf` in `tests\manual\M-01`; both monitors if available. **No other program holds `Ctrl+Alt+H`, `U`, `X` or `M` as a system-wide hotkey** (I-23) -- the NVIDIA overlay held `Ctrl+Alt+M` on this machine.
- **Steps**
  1. From the page with nothing armed, `Ctrl+Alt+H`; highlight a sentence, then one running from the foot of one column to the head of the next.
  2. `Ctrl+Alt+U`, `Ctrl+Alt+X`; *Squiggly*, *Replace text* and *Insert text* from the ribbon.
  3. `Ctrl+Alt+M` with the pointer over the margin; type a note; switch `commenting/popupBehaviour` through its three values.
  4. Repeat steps 1 and 3 on the rotated pages and on the vertical CJK, and on the 100 % monitor.
  5. `Ctrl+Z` back to the start, `Ctrl+Y` forward; save; close; reopen.
- **Expect** Each mark on the glyphs a person meant, on every page and both monitors; a replace or insert reads as one item in the Comments pane; undo and redo step one gesture at a time; after reopening everything is where it was.
- **If it fails** Note the file, the page, the tool and the monitor; a mark off the glyphs is S2.

**M-02 · What other readers show** ☐
- **Goal** D6-6's fourth oracle: Acrobat and Firefox show what RawPage shows.
- **Pre** T8. A copy of `text-a4.pdf` in `tests\manual\M-02`; P-1, P-2.
- **Steps**
  1. In RawPage, make one of every type T6 … T8 provide — highlight, underline, strikeout, squiggly, caret, note, line with heads, arrow, rectangle, oval, polygon, polyline, cloud, ink — with varied colours, one at 50 % opacity, one dashed; save as `m02.pdf`.
  2. Open `m02.pdf` in **Acrobat** and in **Firefox**, side by side with RawPage at the same zoom.
  3. Optionally on a phone (P-2), noting which reader.
  4. As a **control**, open it in Chrome: it should agree with RawPage, because it is PDFium.
- **Expect** In Acrobat and Firefox each annotation the same shape, colour, opacity and position as in RawPage. **Do not edit anything in Acrobat first** — Acrobat may regenerate an appearance it edits (§7).
- **If it fails** Name the type, the reader and what differs; S2, with a screenshot of each in `tests\manual\M-02\`.

**M-03 · A4 through the product — a comment on a signed document** ☐
- **Goal** §3.4's Acrobat half; Stage 3's carried exit criterion.
- **Pre** T6. Copies of the four `corpus/signed/` files in `tests\manual\M-03`; P-1. A-33 green. **No other program holds `Ctrl+Alt+M`** (I-23).
- **Steps**
  1. Open each copy in RawPage; add one sticky note with `Ctrl+Alt+M`; save with `Ctrl+S`.
  2. Open each saved file in Acrobat; open the Signatures panel; *Validate Signature*.
  3. For an approval file, *View Signed Version*.
- **Expect** Each **approval** signature: *"the revision … covered by this signature has not been altered; however, there have been subsequent changes"*, the note listed as the change, and absent from the signed version; **`certified-nochanges.pdf` flagged** — its certification permits no changes.
- **If it fails** Record Acrobat's exact wording per file. An approval signature reported altered is S1.

**M-04 · Exchanging comments with Acrobat** ☐
- **Goal** *FDF/XFDF round-trips with at least one other reader* — with a real one.
- **Pre** T13. A copy of `text-a4.pdf` in `tests\manual\M-04`; P-1.
- **Steps**
  1. In Acrobat, add a note, a highlight, a reply and an *Accepted* status; export comments as **XFDF** and as **FDF**.
  2. In RawPage, open a clean copy and import each.
  3. In RawPage, add a comment and export XFDF; import it into Acrobat on a clean copy.
- **Expect** Each import shows every comment with its author, text, thread and status; the import report lists nothing unplaced.
- **If it fails** Keep the exported files in `tests\manual\M-04\`; they cannot enter the corpus, but they reproduce the failure. S2.

**M-05 · Managing comments in the pane, by keyboard** ☐
- **Goal** The pane's whole table, and its feel, from the keyboard.
- **Pre** T9 -- **walkable since 2026-10-08**. A copy of `annots-threads.pdf` in `tests\manual\M-05`.
- **Steps**
  1. `Ctrl+Shift+C`; walk the list with `↑`/`↓`; `Enter`; `F2`; `Ctrl+Enter` a reply; `Ctrl+Shift+K` a status; `Space` a checkmark; `Ctrl+F` and search.
  2. Group by each grouping; filter by author and by date; sort.
  3. `Ctrl+.`/`Ctrl+,` from the pane and from the page; `Delete`; *Delete matching* under a filter; `Ctrl+Z`.
- **Expect** Every key acts as §8 says; `Space` on the page still scrolls; the page follows the list; threads read naturally; one undo restores a deletion.
- **If it fails** The key, the focus and what happened; S2.

**M-06 · Typed comments** ☐
- **Goal** Free text, callout and typewriter judged where a person types.
- **Pre** T7. A copy of `text-a4.pdf` in `tests\manual\M-06`.
- **Steps**
  1. A text box, a callout pointing at a word, and typewriter text in a margin — in English.
  2. The same in Traditional Chinese and in Japanese.
  3. *(Q3)* Type a Greek word.
  4. Save; open in Acrobat and Firefox.
- **Expect** The text where it was typed, wrapping as typed; CJK drawn with the system's face; the Greek refused with a message naming the letter; the same text in the other readers.
- **If it fails** S2; S1 if a character appears that was not typed.

**M-07 · Stamps** ☐
- **Goal** The palette, custom stamps and dynamic stamps judged by a person.
- **Pre** T10. A copy of `text-a4.pdf` and the `corpus\stamps\` images in `tests\manual\M-07`.
- **Steps**
  1. `Ctrl+Alt+S`; place *Approved* and a dynamic stamp; resize and rotate one.
  2. Create a custom stamp from the PNG and the JPEG; make one a favourite; file one under a new category; add a signature image. *(A PDF page is not a source until Stage 7 — Q7.)*
  3. Paste an image from Paint (`Ctrl+V` on the page).
  4. Quit, relaunch: the library is as you left it. Save; open in Acrobat.
- **Expect** The built-in stamps crisp at 400 %, the image stamps as sharp as their images; the dynamic stamp shows your name and the time of placing; Acrobat shows the same.
- **If it fails** S2.

**M-08 · An audio comment** ☐ *(Q5)*
- **Goal** Recording and playback on real hardware.
- **Pre** T14. P-3. A copy of `text-a4.pdf` in `tests\manual\M-08`.
- **Steps**
  1. *Record audio comment*; speak five seconds; stop; play it back inline.
  2. Attach an existing WAV.
  3. Save; open in Acrobat and play both.
- **Expect** Both play in RawPage and in Acrobat, at the right speed and pitch.
- **If it fails** S2. Audio may slip alone (D6-18): a failure here does not hold the other cases.

**M-09 · Measuring a drawing** ☐
- **Goal** A calibrated measurement with snapping, judged on real monitors.
- **Pre** T12. A copy of `measure-scale.pdf` in `tests\manual\M-09`; both monitors if available.
- **Steps**
  1. `Ctrl+Alt+D`; calibrate on the drawing's labelled line.
  2. Measure a distance, a perimeter and an area, snapping to corners, midpoints and an intersection; at 100 % and 400 %, on both monitors.
  3. Turn `units/snapToObjects` off and measure again; save; open in Acrobat.
- **Expect** The snaps catch at the same on-screen distance at every zoom and monitor; the readouts match the drawing's labels; Acrobat shows the same measurements.
- **If it fails** S2.

**M-10 · Migrating comments to a revision** ☐
- **Goal** The migration and its report judged by a person (§3.5).
- **Pre** T13. Copies of `annots-report-v1.pdf` and `compare\report-v2.pdf` in `tests\manual\M-10`.
- **Steps**
  1. Open `report-v2.pdf`; *Migrate comments* from `annots-report-v1.pdf`.
  2. Read the report; check each migrated comment against where it was in v1.
- **Expect** Every comment whose text survived is on that text; every comment whose text was deleted is in the report and nowhere on the page.
- **If it fails** A comment on the wrong text is S2 and, unreported, S1.

**M-11 · Undo, redo and recovery** ☐
- **Goal** A person's session survives a crash.
- **Pre** T6 (re-walked at T13 with stamps and attachments). A copy of `text-a4.pdf` in `tests\manual\M-11`.
- **Steps**
  1. Make ten annotations of mixed kinds, undo three, redo one.
  2. End the process from Task Manager.
  3. Relaunch; accept the recovery offer.
- **Expect** The recovered document shows exactly the eight annotations that were on screen; `Ctrl+Z` continues from there.
- **If it fails** S1.

**M-12 · Drawing on a 900-page document** ☐
- **Goal** §3.1's judgement half: an edit feels immediate and the page does not stutter.
- **Pre** T8. A copy of `pages-900.pdf` in `tests\manual\M-12`.
- **Steps**
  1. Page 450; draw twenty quick ink strokes.
  2. Scroll fast while drawing; add highlights on pages 1, 450 and 900.
- **Expect** Each stroke appears as it is finished, without a flash; scrolling stays smooth.
- **If it fails** S2, with T4's figures beside the observation.

**M-13 · Attachments** ☐
- **Goal** The Attachments pane, its warning and its refusals.
- **Pre** T11. A copy of `attachments.pdf` in `tests\manual\M-13`.
- **Steps**
  1. `Ctrl+Shift+A`; open the attached PDF; open the `.txt`; try the `.exe`-named file.
  2. Extract each; add a file; delete one; `annot.attachFile` a file as a pushpin.
  3. Save; reopen; quit cleanly and look in `%LOCALAPPDATA%\RawPage\attachments\`.
- **Expect** The PDF in a tab; the `.txt` after a question; the `.exe` refused; extracts equal to what was attached; the session folder empty after the quit.
- **If it fails** S1 if anything executable runs or is written; S2 otherwise.

---

## 7. Known caveats — verify, do not assume broken

Behaviours that look like defects and are not. **A wrong row here is worse than a missing one.**

| Looks broken | Is |
|---|---|
| **Chrome or Edge shows exactly what RawPage shows** | Not evidence — both are PDFium (D6-6). Acrobat and Firefox are the oracles |
| **Acrobat redraws a RawPage annotation differently after you edit it in Acrobat** | Acrobat regenerates the appearance of what it edits. Compare **before** touching anything in Acrobat |
| **An encrypted document's comments do not appear in the pane** | Correct until Stage 10 (D6-3) |
| **A redaction mark leaves the text visible and searchable** | Correct — it is a mark (D6-25) |
| **A Greek or Hebrew letter is refused in a text box** | Correct — Q3, `DW-3` |
| **Opening or moving a pop-up does not make the document *changed*** | Correct — pop-ups are view state (D6-28) |
| **The drag preview looks slightly different from the final annotation for a moment** | The preview is drawn by Qt and released when the engine's tiles arrive (D6-2) |
| **A smooth ink stroke saves its raw points** | Correct — the samples are the data; smoothing is the appearance (D6-15) |
| **An `.exe` attachment cannot be opened** | Correct until Stage 10's trust model (D6-17) |
| **A `Ctrl+Alt+` chord types a character or does nothing** | On a keyboard layout with **AltGr**, `Ctrl+Alt` is AltGr and the layout owns the chord; or another program holds it as a system-wide hotkey (Stage 5's I-17) — try `RegisterHotKey` on it |
| **There is no comment summary** | Stage 8's — Q4 (D6-19) |

---

## 8. Issue log

Numbered `I-nn`, **this stage's own namespace**. Each entry carries its mechanism, why the suite
missed it, and the control that was run red before the fix — the shape every earlier stage's log
uses.

| # | Sev | Title | State |
|---|---|---|---|
| **I-01** | S3 | `corpus/deflate.py` emitted an incomplete Huffman code for a tree deeper than its limit | **Closed** at T1, 2026-10-05 |
| **I-02** | S3 | Plan D6-6 and §3.3 listed *line* among the types PDFium generates an appearance for; it does not | **Closed** at T1, 2026-10-05 |
| **I-03** | S3 | T1's fixtures wrote `/StateModel` and `/State` as names; ISO 32000-1 makes them text strings | **Closed** at T2, 2026-10-05 |
| **I-04** | S3 | T1's commit left `gates.text_bytes.tracked_tree` red: two new binary types the check did not know | **Closed** at T2, 2026-10-05 |
| **I-05** | S3 | A-22's one-second clock graded the `portable-asan` build, which defines `NDEBUG`: CI run 37250978598 red | **Closed** 2026-10-05 |
| **I-06** | S3 | Every revision cancelled every queued tile of the document, whatever its page, though the scheduler's header said only the touched scope's | **Closed** at T4, 2026-10-05 |
| **I-07** | S2 | The overlay's first hit test hit a plain polygon, a polyline and a line three pixels off their strokes, and missed `NoRotate` notes on turned pages | **Closed** at T5, 2026-10-05 |
| **I-08** | S1 | An undo after a save redrew a file's highlight with our builder instead of restoring the producer's appearance | **Closed** at T5, 2026-10-05 |
| **I-09** | S2 | A strikeout drawn through the middle of the line's box sat above the x-height's middle third, and a squiggly 2 points tall spilled out of the descender band | **Closed** at T6, 2026-10-05 |
| **I-10** | S2 | The CJK coverage table claimed a character -- U+223C in Traditional Chinese -- that the engine reads back and draws as nothing on this machine | **Closed** at T7, 2026-10-07 |
| **I-11** | S3 | T7's push did not build in `portable-asan`: GCC 13 under ASan at `-O2` read a one-element initializer list as out of bounds, and `-Werror` made it fatal (CI run 37648525480) | **Closed** 2026-10-08 |
| **I-12** | S2 | The note's `Comment` icon overlapped the bubble PDFium generates for a note at ink IoU 0.877, below D6-6's 0.90 | **Closed** at T8, 2026-10-08 |
| **I-13** | S1 | The Comments pane, hidden, read a stale row's store index after the document changed -- an out-of-range throw from a selection made on the page | **Closed** at T9, 2026-10-08 |
| **I-14** | S3 | A reply or a status to a direct annotation dictionary was refused with no reason -- `/IRT` can only name an object | **Closed** at T9, 2026-10-08 |
| **I-15** | S3 | Opening the Comments pane selected its first comment and sent the page to it -- the list taking the focus read as a person's choice | **Closed** at T9, 2026-10-08 |
| **I-16** | S3 | The Attachments pane WIDENED its window: four command labels side by side raised the window's minimum, and a 1400-pixel window became 1892 | **Closed** at T11, 2026-10-12 |
| **I-17** | S2 | A pushpin whose `/FS` names no file was read holding a NAMELESS file, whose JSON `EmbeddedFile::fromJson` refuses -- the nightly soak's `fuzz_annot_pipeline` aborted (run 38384043765 on `3194c8c`) | **Closed** 2026-10-14 |
| **I-18** | S2 | A file READ with a name or a key past `kMaxAttachmentNameLength` (1,024) could not be moved, edited or deleted -- its pushpin refused by `invalidShape` in `apply()`, its key's delete refused when its journal line was read back | **Closed** 2026-10-14 |
| **I-19** | S2 | A measurement snapped to a crossing that is not there: every point where two pieces of a flattened curve meet end to end was offered as an intersection, outranking the point on the path | **Closed** at T12, 2026-10-14 |
| **I-20** | S3 | A pop-up named `<parent>-popup` in an exchange file collided with a comment of the same file already named so, and the whole import was refused | **Closed** at T13, 2026-10-16 |
| **I-21** | S3 | A pushpin or a sound copied from a file reached the clipboard with no bytes, and the paste was refused (`error.attachment.noData`, `error.sound.noData`) | **Closed** at T14, 2026-10-16 |
| **I-22** | S2 | An FDF RawPage wrote was read back by the repair scan, which takes object text inside a stream's bytes for objects -- a comment read back under a name it was not written under, and an export carrying an attached PDF refused whole on import; the fuzz soak's `fuzz_annot_pipeline` aborted (run 39226515635 on `7f018f9`) | **Closed** 2026-10-19 |
| **I-23** | S3 (environment) | Four of §8's `Ctrl+Alt` arming chords were held by other programs as system-wide hotkeys on the walker's machine -- `Ctrl+Alt+M`, M-01's and M-03's sticky note, by the NVIDIA overlay; `Ctrl+Alt+L`, `Ctrl+Alt+A` and `Ctrl+Alt+K` by owners not found | `Ctrl+Alt+M` **closed** 2026-10-20; the other three **open** -- no manual step presses them |

**I-01 (S3) — an incomplete length-limited Huffman code in the corpus encoder.** *Found* generating
`annots-900.pdf` at T1: `deflate.compress`'s own self-check stopped the run with zlib's *"invalid
literal/lengths set"*. *Mechanism:* `_huffman_lengths`, past its bit limit, clamped every deep code to
the limit and then lengthened the rarest codes until the Kraft sum was at or below one — and that
overshoots: on the object stream's 3.3 MB payload (an 18-deep literal/length tree) it stopped at
0.99960, an **incomplete** code, which inflate refuses as surely as an over-subscribed one. *Why no
suite saw it:* nothing reads the encoder but the corpus, and the repair had only ever been reached by
inputs it happened to complete — `ordinary/image-flate-predictor.pdf` is one, measured when a first
fix that replaced the repair outright moved that file's bytes. *Fix:* the original repair is kept
whenever its code is complete, so **no earlier file moved** (`--verify`, 121 of 121); when it is not,
the code is rebuilt by JPEG's `Adjust_BITS` (ITU-T T.81 Annex K.2) on the unclamped depth counts,
which keeps the tree full, and a completeness assertion follows. *Control:* the captured payload fails
zlib before the fix and round-trips after; Fibonacci-weighted alphabets of 16 to 29 symbols, which
force depths past both limits, give complete codes at 15 and at 7 bits. No shipped file was ever
wrong — the self-check is why.

**I-02 (S3) — a planned oracle that does not exist for one type.** *Found* looking at the T1
capture of `annots-no-ap.pdf` page 1, whose nine annotations have no `/AP`: PDFium 153 draws eight
and **nothing for the line**. PDFium's generator (`core/fpdfdoc/cpvt_generateap.cpp`) covers circle,
highlight, ink, popup, square, squiggly, strikeout, text and underline. *Why it mattered:* A-65 would
have been written at T8 against a control that draws nothing, and an IoU against empty ink is either
a division by zero or a pass. *Fix:* D6-6's clause (2), §3.3's row and A-65 now name **eight** types;
a line's appearance keeps the other three oracles (golden, `qpdf`, Acrobat and Firefox). The page-0
golden baseline of `annots-no-ap.pdf` is a claim about PDFium's generator, as its manifest note says.

**I-03 (S3) — state replies written in the wrong type.** *Found* writing T2's reader: ISO 32000-1
Table 172 makes `/StateModel` and `/State` TEXT STRINGS, as Acrobat writes them, and
`corpus/make-annots.py` wrote them as NAMES. *Why T1 missed it:* its byte check looked for the markers
the generator wrote, so it agreed with the generator. *Fix:* the generator writes strings; the
fixture check looks for `(Review)`, `(Marked)` and the five states; the reader accepts both forms
(producers write names too). `annots-threads.pdf`, its label, and the `threads` exchange files moved;
no pixel did -- state replies are hidden.

**I-04 (S3) — a red gate committed.** *Found* by T2's first full run: `gates.text_bytes.tracked_tree`
failed on every preset, naming control bytes in `corpus/exchange/every-type.fdf`. *Mechanism:*
`tests/gates/check-text-bytes.py` reads every TRACKED file not of a known binary extension, and T1 added
two binary types -- `.fdf`, whose streams carry a sound's raw PCM, and `.jpg`. *Why T1's run missed it:*
the gate reads `git ls-files`, and T1's suite ran BEFORE the files were added, so the commit `96f0e23`
turned it red unseen (it was not pushed). The checker's own header predicted this: *a new binary type
fails this check loudly until its extension is added.* *Fix:* `.fdf` and `.jpg` join
`BINARY_EXTENSIONS`; `.xfdf` is XML and stays checked. *The lesson for a later task that adds a file
type:* run the gates after `git add`, not before.

**I-05 (S3) — a release clock on an instrumented build.** *Found* by CI run 37250978598 on `133a046`,
T2's push: `portable-asan` failed `unit.a million-point ink list is truncated and reported` at
`tst_annotstore.cpp`'s `CHECK(ms < 1000)` -- *"1,000,000-point /InkList: 2571 ms"* -- every other job
green (Windows 1457, `gates` 121). *Mechanism:* `readWithinASecond` chose its ceiling by `NDEBUG`,
taking it to mean *the shipped build*; `portable-asan` is `RelWithDebInfo`, so `NDEBUG` is defined
there too and ASan's instrumentation was graded against the release second. The read was bounded --
truncated and noted as the case requires; only the clock failed. *Why no suite saw it:* MinGW has no
ASan, so the Windows presets compile only the two branches they take. *Fix:* the case detects
AddressSanitizer -- GCC's `__SANITIZE_ADDRESS__`, clang's `__has_feature(address_sanitizer)` -- and
grades a ten-second ceiling there, as loose as `tst_layout`'s million-character case and likewise
grading no speed; `x64-release` keeps the second (203 ms) and `x64-debug` five (1681 ms). *Controls:*
`g++ -fsanitize=address -dM -E` defines the macro and the plain preprocessor does not; the release
ceiling planted at 100 ms failed the case on `x64-release`, so that build still compiles the
one-second branch. **Not yet read:** a `portable-asan` log over the fix.

**I-06 (S3) — a revision cancelled what it did not touch.** *Found* by T4's bench (`bench_pipeline
--stage6`, A-38): an edit on page 450 every 100 ms cancelled **9,051** scrolled tiles of OTHER pages in
30 s, each asked for again at the back of the queue. *Mechanism:* `RenderScheduler::documentChanged`'s
header promised that *"every request in flight or in the queue for the touched scope is superseded"*;
`isSupersededLocked` compared a request's revision with the document's and nothing else, so every
queued, running and finished tile of the document at the old revision was dropped, whatever its page.
The cache had always re-keyed what it held outside the scope (Stage 2 T11); the queue did not. *Why no
suite saw it:* it costs time, not correctness -- the views ask again and the right pixels arrive later --
and every case of it (`tst_renderscheduler`'s supersede cases) put the tile on the page the scope named.
Stage 3's rotations, one at a time by hand, never made it measurable. *Fix:* a tile of the document the
scope does not reach (`render::tileTouchedBy`) is re-keyed to the new revision where it stands -- queued
(joining an equal request already queued at the new revision, if there is one), running (delivered under
its new key) or finished. *Regression cases:* `render scheduler keeps a tile a mutation did not reach at
the new revision` (five sections) and A-34's every-tile comparison. *Controls:* re-keying a tile the
change reached (stale tiles in the every-tile comparison) and a running tile delivered under its old
key (dropped) each failed.

**I-07 (S2) -- a hit test that answered for more than the ink.** *Found* by A-41's first run at T5, the case
written for it, before any commit: on `annots-every-type.pdf` at 2.08 px/pt a plain polygon, a polyline and a
line with arrowheads were hit at points with no ink of theirs within three pixels, and the `NoRotate` notes of
`annots-rotated.pdf`'s 180° and 270° pages were not hit at their own ink. *Mechanism:* `core::hitsAnnotation`'s
first version took a stroke's extra reach -- for clouds and line endings -- from the margin between the
rectangle and the points' bounds, which a producer pads for every shape, and gave a line's endings a DISK the
size of that margin; and the overlay hit-tested a `NoRotate` annotation at its rectangle, where PDFium does not
draw it -- it keeps it upright at the rectangle's upper-left corner (measured at every quarter turn), and does
not honour `NoZoom` at all. *Why the hand-shape cases missed it:* they checked the shapes against the geometry
they were computed from (rule 6's warning). *Fix:* a cloud's reach is its `/RD`, a plain border has none; an
arrow's ending is the wedge its barbs span and any other the disk round its tip (D6-30); the overlay hits and
outlines a `NoRotate` annotation where it is drawn (`AnnotationOverlay::drawnRect`). *Controls:* planted P9 and
P10, each failing A-41's halves.

**I-08 (S1) -- an undo that wrote a different appearance.** *Found* by T5's own A-35 case for edits in place,
before any commit: a highlight from `annots-mixed.pdf` recoloured, SAVED and undone did not draw as the file had
-- 4,338 bytes differed at 72 dpi. *Mechanism:* the undo is an edit from the saved version back to the original
model, and the sink treated it like any colour change: it drew a NEW appearance with our builder in the old
colour, where the original dictionary pointed at the producer's form, still in the file. By the severity table
that is the product changing a byte it did not mean to -- someone else's appearance replaced by ours on an undo.
*Why no earlier case saw it:* T3 and T4 had no in-place edit; the reversibility sweep reverts before any save.
*Fix:* the sink remembers every dictionary an edited annotation has had on disk with the model it read as, and
an edit whose result IS one of those models writes that dictionary verbatim (`MutationSinkImpl`'s header).
*Regression case:* `[I-08]` in `tst_annotedit` -- the restored dictionary equal to the original, `/AP`
reference included, and the update the annotation alone. *Control:* planted P12 failed it and A-35.

**I-09 (S2) -- two text lines drawn where PDFium draws them, and off the glyphs.** *Found* by T6's own A-50
case, before any commit: on `text-a4.pdf` at 2.78 px/pt a strikeout's ink was centred at row 257.1 against an
x-height middle third of 258-262 -- above it -- and a squiggly's ink spanned rows 297-305 against a descender
band of 298-303. *Mechanism:* the builder drew both as PDFium's own generator does (`cpdf_generateap.cpp`, read
for D6-6's second oracle): the strikeout through the middle of the quadrilateral and the squiggle 2 points tall
at every size. The quadrilateral is the line's box, which reaches the font's ascent -- well above the x-height
-- so its middle is not the x-height's; and 2 points is more than the whole descender band of 10-point text.
*Why the unit cases missed it:* they checked the strokes against the quadrilateral they were drawn from (rule
6's warning, again); A-50 measures them against the glyphs' INK. *Fix:* the strikeout 42 % of the way up the
box -- the fraction the corpus generator draws, between the two fonts' x-height middles -- and the squiggle
between 7 % and 16 % of it, in a stroke of a 28th; the underline stays PDFium's (D6-32). *Consequence recorded,
not resolved:* §3.3's second oracle -- our appearance against PDFium's own at ink IoU 0.90 -- cannot hold for a
strikeout or a squiggly drawn on purpose where PDFium does not draw them; T8, which completes `tst_appearance`,
grades those two by A-50's measure instead, and says so. *Controls:* planted P2 and P3 (the T6 row) put each
back where PDFium draws it, and each failed A-50.

**I-10 (S2) -- a typed character the coverage table accepted, drawn as nothing.** *Found* by T7's own A-56 engine
case, before any commit: of the 59,270 characters `core/doc/CjkCoverage.inc` first claimed, PDFium read every
one back as written, and one -- U+223C TILDE OPERATOR in Traditional Chinese (`MSung-Light`, Big5 0xA1E3) --
drew no ink in the face the product's font substitution chooses on this machine (PMingLiU). A person typing it
into a comment in Traditional Chinese would have seen it accepted and shown nothing: *text that is not there*,
Stage 5's S1, short of S1 only because the character reaches the file as itself. *Mechanism:* the table is
built from each collection's legacy character set as Python's codecs hold it -- a proxy for the collection, and
a proxy for the face that draws it -- and Big5 maps 0xA1E3 to a character this face has no glyph for under that
CID. *Why the unit cases missed it:* they read the table's own claims (rule 6, in a new form: a claim checked
against the source it was generated from agrees with itself); only the engine knows what it draws. *Fix:* the
character excluded in the GENERATOR (`EXCLUDED`, with the date, the machine and the face it was measured on),
the table regenerated, never edited -- and the first run's ink threshold, half intensity, corrected to any
visible ink after eight hairline glyphs (box-drawing bars, `∣`, `∥`) read as no ink at 24 pixels an em, and
the two characters blank by definition (U+3000, U+3164) exempted. *Bound:* the measurement is of THIS
machine's faces; another machine's substitution may differ, and M-06 is where Acrobat's faces are seen.
*Control:* planted P5 put the character back in the table, and the engine case failed on it.

**I-11 (S3) -- T7 did not build where the sanitizers run.** *Found* by CI run 37648525480 on `99b636f`, T7's push:
four of five jobs green (`gates`, Windows, `portable-fuzz`, the coverage summary) and **`portable-asan` red at its
build step** -- so the headless suite, and A-31's `qpdf` step after it, never ran. *Mechanism:*
`pdf/ops/AppearanceStreamBuilder.cpp`'s free text reset an unusable text colour with `textColour = {0.0}`; GCC 13 at
`-O2` with `-fsanitize=address,undefined` inlines the initializer list's copy into a `memmove` it then judges to read
past the one-`double` array (`-Warray-bounds`, a false positive), and the portable presets' `-Werror` made it an
error. *Why nothing here saw it:* no Windows build sanitizes (MinGW has no ASan runtime), and the warning appears
only with the sanitizer flags -- Stage 3 guide I-08's shape, a different header. *Reproduced on this machine*,
which it can be: MinGW's GCC 13.1 COMPILES with `-fsanitize=address,undefined` even though it cannot link it, so
the object's own command (`ninja -t commands <object>`) with those flags and `-Werror` added gave the same error, and
every other portable file T7 touched -- and its three unit test files -- compiled clean the same way. *Fix:*
`textColour.assign(1, 0.0)`, after which the probe compiles; the Windows suites are unchanged (`unit.` 737/737 on
`x64-release`, both presets building with 0 warnings). *Owed:* the `portable-asan` log of the next run, which is
the first to run T3's … T7's headless cases under ASan and `qpdf` over their files.

**I-12 (S2) -- the note's icon and PDFium's disagreed past D6-6's tolerance.** *Found* by T8's own A-65, before any
commit: RawPage's `Comment` bubble -- drawn at T6 on the corpus generator's proportions, an outline of 0.6 units, its box
from 5 to 19 of a 20-unit grid, its tail at the far left -- overlapped the bubble PDFium generates for the same note
with no `/AP` at **ink IoU 0.877** against D6-6's 0.90 (ours 1,163 pixels at 144 dpi, PDFium's 1,319, 1,160 shared:
PDFium's box is taller and its outline twice as heavy). *Measured first* with `Note`, at 0.667, and that run showed
why: PDFium draws ITS OWN bubble for EVERY note, whatever `/Name` says, in a fixed yellow -- so the grade is of the
shape RawPage's tool writes, `Comment`. *Why nothing saw it before:* the oracle did not exist until T8 -- T6's notes
were held to the ink of where they were clicked (A-53), not to another reader's drawing. *Fix:* the `Comment` icon
redrawn on PDFium's proportions, as its generated bubble draws (measured against its raster at T8): a one-unit
outline, the box from 0.5 to 19.5 across and 4.5 to 19.5 up, the tail from 4.5 to 8.5 down to 6.5, three lines at its
quarters two units in -- in `/C`, not PDFium's yellow; IoU **0.999** after. Its top-left ink corner is now the
rectangle's, where the note was clicked. *Bound:* PDFium is one reader; Acrobat's own `Comment` icon is M-02's to
compare. *Control:* planted P15 put T6's bubble back, and A-65 failed on it.

**I-13 (S1) -- a stale row read after the document changed.** *Found* by T9's run of an EARLIER case, before any commit: `tst_annotoverlay`'s A-42 (an `Alt`+drag duplicate, then an undo) died with *vector::_M_range_check: __n (which is 4) >= this->size() (which is 4)*. *Mechanism* (gdb, `catch throw`): the undo removed the duplicate; the overlay's selection changed; the window made the selection the Comments pane's current comment; the pane -- HIDDEN, so its list had not been rebuilt since the store changed -- rebuilt and first asked its current row which comment it was, by the STORE INDEX the row had been built with, which no longer existed. In the product an uncaught throw from a slot ends the process. *Why nothing else saw it:* the pane's own cases rebuilt it while shown; only a pane hidden through a change, then reached through the page's selection, read a row older than the store. *Fix:* every row carries its annotation's IDENTITY beside the index; the pane resolves a row through the identity (`AnnotationStore::find`), and the model answers nothing for a row whose annotation is not where it was. *Regression:* `tst_commentspane`, `[I-13]` -- the pane hidden behind another, two comments deleted under it, a selection made on the page: the current comment and the list are the store's. *Control:* planted P12 (the row's index read raw) failed it.

**I-14 (S3) -- an answer to a direct dictionary refused with no reason.** *Found* by T9's reversibility sweep (A-29), before any commit: on `annots-direct.pdf` page 1 a reply and a status to the first annotation -- a DIRECT dictionary in `/Annots` -- *apply returned false*. *Mechanism:* `/IRT` can only name an indirect object (ISO 32000-1 Table 170) and a direct dictionary has none until its first edit promotes it at compose (D6-4); the command refused it in `apply()`, where the only thing a person would read is *the change failed*. *Fix:* both commands' `permitted()` refuse it by key, `error.edit.annotationDirect`, the page named (D6-36(c)); the sweep's generator answers only what may be answered. *Regression:* `tst_annotthreads`, `[D6-36]`. *Control:* planted P14 (the refusal removed) failed it.

**I-16 (S3) -- the Attachments pane widened its window.** *Found* by T11's first full run, before any commit, in a case written at Stage 4: `tst_restore`'s *rememberWindowLayout off* set a window 1400 pixels wide and read back **1892**. *Mechanism:* a pane in the sidebar dock adds its minimum width to the window's; the new pane's top row held four tool buttons each carrying its command's whole label (*Save Attachment As...* among them), and at the offscreen platform's font metrics the row alone asked for some 490 pixels more than the window had -- Stage 5's I-10, the find bar's row, in a new place. *Why nothing else saw it:* the pane's own cases checked what it lists, not what it costs its window, and a pane added to the sidebar is a change to every window's minimum. *Fix:* each button says ONE word (`attachments.button.*`), carries its command's label as its tooltip, and has an `Ignored` horizontal size policy, so its minimum is no claim on the pane's width (D6-38(k)): the pane's minimum is **108** pixels against the Comments pane's 414 beside it. *Regression:* `tst_attachments` (`ui.`) -- the pane's minimum no wider than the Comments pane's, and a 1400-pixel window still 1400 with it shown; `tst_restore` green again. *Control:* planted P10 (the full labels and the default policy put back) failed it.

**I-15 (S3) -- opening the pane moved the page.** *Found* by `[I-13]`'s regression case while it was written: the overlay already held the comment the case selected, because `Ctrl+Shift+C` had selected it. *Mechanism:* a `QTreeView` makes its first row current when it takes the focus with nothing current, and the pane treated every current-row change as a person's choice -- so opening the pane selected the first comment on the page and scrolled there. *Fix:* a change FROM nothing current is not a gesture; a click is, and so is every move from one row to another (D6-36(f)); a gesture is activated once. *Regression:* `tst_commentspane`'s A-70 asserts opening the pane leaves the page's selection empty and the page where it was. *Control:* planted P13 (every change a gesture) failed it.

**I-17 (S2) -- a pushpin read holding a file with no name.** *Found* by the nightly fuzz soak, run 38384043765 on `3194c8c` (T11's commit), 2026-10-13 -- the first soak over T11's reader: `fuzz_annot_pipeline` aborted after 12,564 runs with *"annotation pipeline invariant violated: an annotation's JSON does not read back"* (A-23's JSON invariant); the other four targets were clean. *Mechanism:* the input's `FileAttachment` named `/FS 42 0 R`, and object 42 was a Form XObject -- a dictionary with no `/UF` and no `/F`. `pdf/ops/EmbeddedFileReader::fileOf` reads such a specification as a `core::EmbeddedFile` with an EMPTY name (as it does `/FS ()`), and `pdf/ops/AnnotationReader` kept it as the pushpin's `attachment`; `EmbeddedFile::fromJson` refuses a file with no name, and so does `AnnotationCommands`' `invalidShape` -- so outside the fuzzer, an `EditAnnotationCommand` over such a pushpin (a recolour, a lock), which checks its new state with `invalidShape` in `apply()` and serializes the whole annotation, was refused, and its journal line could not have been read back. A document-level file never met it: its name falls back to its KEY (`EmbeddedFileReader::attachments`), which a pushpin does not have. *Why nothing saw it before:* T11's cases read pushpins the corpus generator writes, every one named; A-23's replay runs the body over the corpus, which by construction finds nothing new (convention 19), and the soaks after T11's commit until this one ran on `f72117c`, T10's. *Fix:* the reader holds NO file for a pushpin whose specification names none (`pdf/ops/AnnotationReader.cpp`) -- the precedent `EmbeddedFileReader::attachments` set for a key nobody can name. The annotation is kept, drawn and edited as it was, and the Attachments pane, which lists only pushpins holding a file, does not list it; its `/FS` is never rewritten from the model, so a save keeps what the file said. *Regression:* `tst_annotstore`'s *a pushpin whose file specification names no file holds no file and its JSON reads back* (`[I-17]`): three nameless shapes -- a `/Filespec` with no name, `()`, and a reference to a Form XObject as the soak's input had -- each a `FileAttachment` holding no file, a named `/FS (kept.txt)` as the control that a named file is still kept, every annotation's JSON round trip equal, and the fuzz body over the document clean. The soak's own input, replayed through the body on Windows by a temporary case: *an annotation's JSON does not read back* before the fix, clean after (21 annotations). *Control:* the fix's one line removed -- the regression case failed four assertions and the soak's input failed with the soak's message. `ctest` **1705/1705** on `x64-release` and on `x64-debug`, the same four named skips; `checks` clean; the changed file compiled under `-O2 -fsanitize=address,undefined -Werror`. *Bound:* the soak re-run over the fix is owed -- the nightly run after this commit is its reading.

**I-18 (S2) -- a file read with a name or a key past the bound could not be moved, edited or deleted.** *Found* reading the code while fixing I-17, 2026-10-14 -- not by a run: `core::kMaxAttachmentNameLength` (1,024) was applied to EVERY attachment a command named, while neither `pdf/ops/EmbeddedFileReader` nor `pdf/ops/AnnotationReader` bounds what it reads. *Mechanism, two paths:* (a) a pushpin whose file specification names a file of more than 1,024 characters -- `AnnotationCommands`' `invalidShape` refused any attachment so named, and the move, the edit and the appearance edit each call it on the annotation's new state in `apply()`, so moving, recolouring or locking that pushpin was refused, and an `EditAnnotationCommand` line holding it was refused when read back; (b) a document-level file whose KEY is longer -- `DeleteAttachmentCommand::apply` deletes any key the model finds and so deleted it, but `deserialize` refused a key past the bound, so the journal line could not be read back and a recovery would have stopped at it. *Why nothing saw it before:* T11's cases read the generator's files, every name and key short; the fuzzer cannot reach it, because `Annotation::fromJson` -- A-23's JSON invariant -- bounds no length. *Fix:* the bound binds what a command CREATES -- a file RawPage attaches (`carried`), its name in `invalidShape` and its key and name in `AddAttachmentCommand` -- and not a file it only NAMES from the document: `invalidShape` bounds a carried file's name and keeps refusing a file with no name, and `DeleteAttachmentCommand::deserialize` takes any key but the empty one. Neither reader truncates: a name cut at 1,024 characters would lose its extension, and `core::isExecutableName` reads the policy off the last extension. *Regression:* `tst_attachments`' *a file read with a key or a name past the bound a created one keeps is deleted moved edited and replayed* (`[I-18]`, `unit.`): a built document holding a 2,004-character key and a pushpin naming a 2,004-character file -- the delete, the edit and the move each read back from their own serialization and apply, the delete and the edit undone, the moved pin saved and reopened with its file's name whole; and its control, a CREATED pushpin carrying a file so named, refused. `tst_command_serialize`'s hostile attachment case gains *a name past the bound* and *a key past the bound* for `attachment.add`. *Run before the fix:* the case failed on the delete's line (`attachment.delete.keys`) and on the edit, read back and in `apply()` (`annot.edit.annotation.attachment`). *Controls:* planted P1 (the carried file's name bound removed from `invalidShape`) failed the case's control; P2 (`AddAttachmentCommand::deserialize`'s key bound removed) failed *a key past the bound*. `ctest` **1706/1706** on `x64-release` (its first full run red once on Stage 5's A-88 wall clock, 3.859 against 3.587 ms, in `tst_foldersearch`, which this change does not touch -- green alone five times and in the next full run) and on `x64-debug`; `checks` clean; both changed `core/` files compiled under `-O2 -fsanitize=address,undefined -Werror`. *Bound:* the same mismatch between what the reader keeps AS WRITTEN and what `invalidShape` accepts exists for other fields, from reading the code and not from a run -- `/NM` over 256 characters, a `/BS /S` or `/BM` name it does not know, a dash array of zeros or with a negative, an opacity outside 0-1 -- and is not fixed here.

**I-19 (S2) -- a curve snapped to a crossing that is not there.** *Found* by T12's own case before any commit: `tests/render/tst_measuregeometry`'s *a form's paths are read through its matrix and a curve's ends are its anchors* aimed at the middle of a Bezier, expecting a point ON the path, and got an INTERSECTION (`2 == 3`). *Mechanism:* `core::snapPointsNear` offered every point where two nearby segments meet as a crossing -- and the engine flattens a curve into eight pieces that meet end to end, so every eighth of every curve was a crossing target, outranking the point on the path a person was aiming at; a polyline's corners were offered twice, as an endpoint and as a crossing (harmlessly, the endpoint winning the tie). *Why nothing else saw it:* the headless snap cases were written over the corpus drawing, all straight segments whose meetings are corners; only a curve read by the engine has pieces that meet without being corners. *Fix:* two segments that only meet END TO END do not cross -- a crossing is offered only where the point is not an end of both (`core/Snap.cpp`); a T, where one segment ends on the middle of another, still is. *Regression:* `tst_snap`'s *pieces of a path that meet end to end are no crossing* (`[I-19]`): a curve's three pieces and a polyline's corner offer no crossing, and two crossing segments and a T each do -- the controls; and the engine case above. *Control:* planted P14 (the condition put back) failed both.

**I-20 (S3) -- a pop-up named like a comment of the same file.** *Found* while writing `fuzz_annot_pipeline`'s exchange branch (T13), before any run: the branch's invariant says every annotation an exchange file reads has a name of its own, and reasoning about what could break it found this. *Mechanism:* an exchange names references by NAME, so a pop-up, which has no name in XFDF and usually none in an FDF, is named `<parent>-popup` (`core::popupNameFor`, D6-40(a)). A file holding a comment ALREADY NAMED `a-popup` beside a note `a` with a pop-up read as two annotations under one name, and the import's one `ImportAnnotationsCommand` refused WHOLE (`annotation.id`), so a single odd name blocked every comment of the file. *Why the suite missed it:* no fixture names a comment like a pop-up, and A-93's round trip writes only the names RawPage gave. *Fix:* in both readers (`core/doc/XfdfCodec`, `pdf/ops/FdfCodec`) a pop-up whose name is taken is dropped and a note says so -- the comment is the person's, the pop-up only shows its words -- and the merge checks the pop-up's name against the document's comments as well (`AnnotationStore::importFdf`). *Regression:* `unit.a pop-up never takes a name a comment of the file already has` (`tst_fdf`, `[I-20]`), with its control: the comment renamed, the pop-up kept. Planted P20 (the reader's check removed) failed it. Closed 2026-10-16.

**I-21 (S3) -- a pushpin or a sound copied from a file pasted with no bytes, and refused.** *Found* at T14, reading the copy path (`MainWindow::copyAnnotations`) to back D6-41(k)'s claim that a paste carries a sound as it carries any other kind, before any commit -- and confirmed by a case that failed first: the paste made nothing (`journal position 0 == 1`). *Mechanism:* the model holds what a file attachment and a sound SAY, and their bytes only for ones RawPage made (D6-38(b), D6-41(f)); the copy wrote each annotation's JSON as the store holds it, so one READ from the file reached the clipboard with no bytes, and the paste's `importCheck` refused it by key (`error.attachment.noData`, `error.sound.noData`) -- said, but nothing pasted. T13's pushpin had the same gap; T14's sound would have joined it. *Why the suite missed it:* A-81's T13 cases copied a highlight, a square and a note -- kinds with no payload -- and the pushpin was never copied from a file. *Fix:* the copy reads a file attachment's bytes and a sound's samples from the document first (`IDocumentBackend::attachmentData`, `soundSamples`), as an export does (`writeAnnotationExchange`). *Regression:* `ui.composition.a pushpin and a sound copied from a file are pasted with their bytes` (`tst_exchangeproduct`, `[I-21]`): one command, saved, reopened fresh, each payload's SHA-256 the label's. Closed 2026-10-16.

**I-22 (S2) -- an FDF we wrote read back by the repair scan.** *Found* by the fuzz soak, run 39226515635 on `7f018f9`, 2026-10-19 -- the first soak over T14's code: `fuzz_annot_pipeline` aborted after 3,808 runs with *"annotation pipeline invariant violated: an FDF read back under a name it was not written under"* (A-23's EXCHANGE invariant); the other four targets were clean. *Mechanism:* `pdf/ops/FdfCodec::write` gave its FDF no cross-reference table -- ISO 32000-1 §12.7.8.1 lets an FDF leave it out -- so `FdfCodec::read` met its own output through `pdf/cos/XrefRepair::openWithRepair`'s scan for `N G obj`, which walks a stream's bytes, as it must over a damaged file, and keeps the LAST definition of a number. The soak's input, an FDF spliced into another, carried a `/Sound` whose 4,000 bytes held other objects' text; read, its samples were carried, and written back they redefined five annotations, which then read back under the names in the samples. *Not only a fuzzer's shape:* a pushpin carrying a PDF -- whose bytes spell `1 0 obj << /Type /Catalog ...`, where the FDF's own catalog is object 1 -- made an export that the import refused whole (`error.exchange.malformed`, no `/FDF`). *Why nothing saw it before:* A-93's round trip and every exchange case carry files and sounds whose bytes spell no object, and A-23's replay runs the corpus, which by construction finds nothing new (convention 19); T14 made a sound's samples a second stream an FDF carries, and the soaks after T13's commit ran no further until this one. *Fix:* the writer records each object's offset and ends the file with a classic cross-reference table, `/Size` and `startxref`, so `openWithRepair` reads the objects where the writer put them; the scan is left for the files other producers write without one (`pdf/ops/FdfCodec.h` says why the table is there). *Regression:* `tst_fdf`'s *an FDF we write reads back as written whatever a stream of it spells* (`[I-22]`, `unit.`): a square, then a sound whose samples spell an object 2 named `impostor` -- both names and the square's rectangle back as written, the samples whole, and the fuzz body's exchange branch clean over the written file; and a pushpin carrying a PDF read back with its file. The soak's own input, replayed through the body on Windows by a temporary case: the soak's message before the fix, clean after (15 annotations, every name kept). *Control:* the fix removed -- the case failed three assertions (the names, the rectangle, and the PDF-carrying export refused), and the soak's input failed with the soak's message. `ctest` **1809/1809** on `x64-release` and on `x64-debug`, the same five named skips; `gates` 137/137; `checks` clean; the changed file compiled under `-O2 -fsanitize=address,undefined -Werror`. *The soak over the fix:* run 39267194087, dispatched on `bc2bb44` 2026-10-19 -- all five targets clean, `fuzz_annot_pipeline` 15,802 runs; its one artifact a SLOW unit, not a finding: 13 s under the soak's instrumentation, the first this target has reported, and on Windows 0.68 s for 8,156 annotations against 0.33 s for 9,000 in the seed it was bred from, `ordinary/annots-900.pdf` -- a mutation of 22,934 bytes that most likely sent the open down the repair path, the same order of cost. *Bound:* an FDF another producer writes without a table is still read by the scan, and a stream of it that spells an object can still replace one, which is what a damaged or hostile file is owed rather than a defect of ours.

**I-23 (S3, environment) -- four arming chords held by other programs on the walker's machine.** *Found* preparing M-01's walk, 2026-10-20, before the product was opened: §7's caveat says to try `RegisterHotKey` on a chord a walk presses, and `RegisterHotKey(NULL, id, MOD_CONTROL | MOD_ALT, 'M')` failed with `ERROR_HOTKEY_ALREADY_REGISTERED` (1409), while `Ctrl+Alt+H`, `Ctrl+Alt+U`, `Ctrl+Alt+X`, `Ctrl+Z`, `Ctrl+Y`, `Ctrl+S` and `Ctrl+K` registered and were released. Re-run with `rawpage.exe` open, with and without `MOD_NOREPEAT`: still 1409. *Mechanism:* not RawPage's, and the same as Stage 5's I-17 (`Stage5-TextSearch-Testing-Guide.md` §8): Windows gives a registered hotkey to the program that registered it, as `WM_HOTKEY`, before the key is queued to any window, so `ShortcutBinder` never sees the `M`. Windows does not name a hotkey's owner; **the user found it -- the NVIDIA overlay -- and changed it**, after which `Ctrl+Alt+M` registered and was released (02:35). *Swept then:* every `Ctrl+Alt` chord §8 (`Features.md`) binds in the Viewer -- `M H U X T L R O N P S D A K C` -- and **`Ctrl+Alt+L` (Line), `Ctrl+Alt+A` (Area measurement) and `Ctrl+Alt+K` (Mark for redaction) were held**, owners not found; the NVIDIA App, the NVIDIA overlay, AMD's Radeon Software and Logitech's lighting service were running. No manual step names those three chords -- M-02's line and M-09's area say what to make, not how to arm it, and the ribbon arms both -- so they block no walk, but a walker who reaches for one will find it dead; they stay open until their owners are found. *Not measured:* whether a press of `Ctrl+Alt+M` in the product did nothing while the chord was held -- the walk's step 3 is the first press since it was released. *Why the suite cannot see it:* every widget case sends its `QKeyEvent` straight to the widget, below Windows' input queue, and no test can know what another program on a person's machine has registered. *Not fixed in code, on purpose:* §8 keeps every chord -- `Ctrl+Alt+M` is Q1's (`Stage6-Annotations-Plan.md` §0), the user's decision -- and a person who keeps another program's hotkey can remap the action on Preferences' Shortcuts page. M-01's and M-03's **Pre** now say the chord must be free.

---

## 9. Sign-off criteria

Stage 6 is done when **all** of these hold — [`Stage6-Annotations-Plan.md` §13](./Stage6-Annotations-Plan.md#13-definition-of-done-for-stage-6)
restated as a checklist.

1. ✅ **2026-10-04.** The plan's §0 answered — all eight, the day it was written — and every case
   written against a proposal re-read against its answer: all stand as written, A-40's Expect gaining
   the census at thirteen (Q1) and A-78 and M-07 rewritten (Q7).
2. ✅ **2026-10-20, T15**, with two halves owed to runs that are not this machine's to make: every automated case carries a
   result, recorded against it rather than summarized (Stage 1's I-32), the Linux halves read from the job's **log** --
   **owed:** A-101's by-hand recording run on the `windows` platform (it records from the microphone and plays aloud, so it
   runs with your leave), and the first `portable-asan` log over T15's two new `tst_redaction` cases (A-04).
3. ☐ M-01 … M-13 walked and signed off **by you**. I do not mark a manual case.
4. ✅ **2026-10-20, T15** (A-08, A-10): `checks` clean — `mutation-lint` naming exactly the plan's commands (A-08), **15** waivers (A-10).
5. ✅ **2026-10-20, T15** -- 1816/1816 on each preset, `gates` 137/137, four renames said (A-102); the UI binary whole eight
   times (A-106): `ctest` green on **both** presets and the `gates` preset, every earlier case present by name
   (A-102); the UI binary green whole (A-106).
6. ✅ **2026-10-20, T15**, read from CI run 39267069191's logs and soak 39267194087's -- T15's own push owed its run:
   `portable-asan` clean over every Stage 6 suite (A-04); `portable-fuzz` and the soak clean on
   **five** targets (A-23); `qpdf --check` clean (A-31).
7. ✅ **2026-10-20, T15** (315 earlier entries identical, 99 added): The golden suite green with every existing baseline unchanged (A-103).
8. 🟡 **A2 at Stage 6** recorded, bench and product (A-38, A-104) ✅ **2026-10-20, T15** -- `go, with named custom work`,
   both readings outside §3.1, the rest deferred as `DW-4`; **A4's product half** recorded (A-33 ✅, **M-03 not walked**).
9. ✅ **2026-10-20, T15**: no open S1 or S2 in §8 -- I-01 … I-22 all closed, and T15 raised none.
10. ✅ **2026-10-20, T15**: `Architecture-FileMap.md` rows flipped with their files; the plan's §10 debt paid;
    `Implementation-Plan.md`'s Stage 6 block updated to reality, deviations included.
