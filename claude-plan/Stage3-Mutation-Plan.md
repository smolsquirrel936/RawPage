# Stage 3 — Document Model, Command Journal, Incremental Save · Execution Plan

> **What this document owns.** The **execution order** of Stage 3 and the **Stage-3-local design
> choices** that no other document decides. It owns no product facts. Every stack, layering,
> key-binding, and verification fact is cross-referenced to its owner and never restated as a new
> decision:
>
> | For… | Read |
> |---|---|
> | The Stage 3 objectives, components, key bindings handled, settings keys, and exit criteria | [`Implementation-Plan.md` § Stage 3](./Implementation-Plan.md#stage-3--document-model-command-journal-incremental-save) |
> | The mutation-through-commands design, the on-disk formats, the fallbacks, risk **A4** | [`Tech-Stack-Plan.md` §3.3, §5](./Tech-Stack-Plan.md#5-the-headline-cross-cutting-concern-designed-end-to-end-mutation-through-commands) |
> | What each file is for, and flows D and F | [`Architecture-FileMap.md` §3.2, §3.3, §3.9, §3.11, §5](./Architecture-FileMap.md#d-a-mutation-end-to-end--the-cross-cutting-live-path-annotated) |
> | The authoritative key bindings — `Ctrl+S` (Global), `Ctrl+Z`/`Ctrl+Y` (Common), `Ctrl+R` (Viewer, Page Thumbnails) | [`Features.md` §8](./Features.md#8-key-bindings-authoritative-reference); the capabilities in §2.5, §5.1, §9.2 |
> | How Stage 3 is verified | [`Stage3-Mutation-Testing-Guide.md`](./Stage3-Mutation-Testing-Guide.md) — written from this plan, **before** the code |
>
> **Status:** ⚪ **Written 2026-09-14, the day Stage 2 was signed off. No Stage 3 code exists.** This
> file is a plan, not a progress log: Stage 3's live status, task by task and deviation by
> deviation, will be the **Stage 3 status block in
> [`Implementation-Plan.md`](./Implementation-Plan.md#stage-3--document-model-command-journal-incremental-save)**,
> which is the authority. Stage 2's plan carried a per-task narrative until its T16 and removed it
> then, having watched one file disagree with itself in three places; this one does not start one.
>
> **Four decisions below were made by the user on 2026-09-14, before this plan was written**:
> **D3-4** (save through a temporary copy and an atomic replace), **D3-2** (A4's evidence is the
> OpenSSL command-line tool and Acrobat), **D3-8** (`Outline` and `Metadata` go to Stage 4) and
> **D3-9** (two of the four settings keys leave Stage 3). Each row says so.
>
> **Label scoping, because three namespaces collide across stages.** `T1`…`T13` here are **Stage 3**
> tasks and are unrelated to Stage 0's, Stage 1's or Stage 2's; this document writes "Stage 3 T4"
> wherever the stage is not already obvious. `D3-n` are this stage's decisions, distinct from
> `Tech-Stack-Plan.md`'s `D1`…`D10` and from `D2-n` (`Stage2-Rendering-Plan.md` §3). `A4` is a
> **risk** (`Tech-Stack-Plan.md` §3.3); `A-nn` are **test cases**
> (`Stage3-Mutation-Testing-Guide.md` §5). A bare `I-nn` in this file is **Stage 2's**
> (`Stage2-Rendering-Testing-Guide.md` §8) unless it says otherwise.

---

## 1. What Stage 3 is actually for

Stage 2 made the product look at a document. Stage 3 makes it change one — rotate a page, undo it,
redo it, save it, reopen it, and get it back after a kill — and that is the last stage of the first
milestone, the one after which *"everything is work, not risk"*. Three things get permanently harder
if this stage gets them wrong.

1. **The A4 verdict — do the original bytes survive a save?** A signature covers a byte range. If an
   unrelated later edit disturbs one byte inside it, every signed document a user ever edits with
   this product is silently broken, and the defect surfaces at Stage 11 as *"our signatures do not
   survive"* — nine stages after the writer that caused it. `pdf/cos/IncrementalWriter` is the
   riskiest first-party code in the project (`Implementation-Plan.md` § Stage 3) and it has one job
   that cannot be approximated. → **D3-1**, **D3-2**, **D3-4**, **D3-5**.
2. **The shape of every mutation that follows.** `RotatePageCommand` is the first of about a hundred
   commands. Whatever it does — how it reaches the model, how it expresses intent to `MutationSink`,
   how it serializes, how its inverse is proven, how `ui/` is kept from touching the document — every
   later command copies. Flow D (`Architecture-FileMap.md` §5) is the design; this stage is where it
   stops being a diagram. `mutation-lint` turns on here so the copy cannot drift. → **D3-10**,
   **D3-12**, **D3-13**.
3. **Recovery that holds at an arbitrary instant.** *"Survives a kill"* is a claim about **every**
   instant, and the instants that matter are the ones a demonstration never picks: between `apply()`
   and the journal line, half-way through a line, between writing the temporary file and replacing
   the original. A recovery design that is right at the moments someone thought of is a recovery
   design that loses work. → **D3-4**, **D3-11**, **D3-14**.

**What Stage 2 should change about how this stage is built.** Stage 2 produced 41 issues, and its
second half repeated three lessons that apply here with more force, because Stage 3 writes to the
user's files:

- **A green tick is not a clean run** (I-38). Every fuzz run for a week logged a sanitizer finding
  under a passing job. For a writer the equivalent is a round-trip case that compares the **buffer
  it wrote** rather than **the file on disk after the replace** — green, and blind to exactly the
  step D3-4 adds. Every byte-identity assertion in the guide is made against bytes read back from the
  path, and says so.
- **An instrument needs a known answer before its readings mean anything** (I-41). A signature
  validator that reports *valid* has to be shown reporting *invalid* for a file whose signed range was
  changed by one byte, or its *valid* is a dead instrument. The same holds for the kill harness: a
  kill that lands only between commands proves nothing about a kill inside one.
- **A mechanism needs a control written from what must stay untouched** (convention 22,
  `Implementation-Plan.md` §1.5, found by I-13). Rotating page 3 must leave page 4's object, the
  `/Pages` node, `/Info`, every stream and every byte before the update exactly as they were — and
  undo before save must write **nothing**. Those are the controls, and the criterion will not ask for
  them.

Stage 1's convention 19 still applies without change: a case that reaches rotation by calling the
journal directly has not pressed `Ctrl+R`, and says so at the assertion.

---

## 2. Inherited ledger — what Stage 2 hands over

Recorded so the tasks below are concrete. None of these is re-decided here.

| Fact | State at the end of Stage 2 | Consequence for Stage 3 |
|---|---|---|
| The suite | **741 cases** on `x64-release` by `ctest -N`, re-measured 2026-09-14: `unit.` 346, `ui.` 207, `gates.` 94, `render.` 55, `platform.` 36, `bench.` 3. Green, one skipped by name (the battery case) — Stage 2 I-39 and I-40's run | Stage 3 adds to a suite that is green; a red case is this stage's |
| `core/backend.h` | Declares `MutationSink` with **one** method, `Status setPageRotation(int pageIndex, int degrees)`, and no save method at all. `IDocumentBackend::mutationSink()` returns null | T4 implements the sink; T8 adds the save method. Flow F's `IDocumentBackend.save(Incremental)` is a shape, not a signature |
| `pdf/DocumentBackendImpl` | Implements **2 of 7** interfaces. Lives in `pdf/` but compiles into `rawpage_pdfium` (Windows), because a backend that opens documents is the engine (D2-2) | The mutation half must not follow it there, or the writer never runs under ASan — **D3-3** |
| `pdf/render/PdfiumDocument` | Loads the file as **bytes** (`FPDF_LoadMemDocument64`) and keeps them; builds the `PageTree` **from PDFium**; runs the COS reader over the same bytes only to produce a `RepairReport` | The COS reader's view of the pages is not currently kept or compared with the engine's — **D3-7** makes their agreement a precondition of editing |
| `core/doc/` | Five vocabulary types (D2-5): `Page`, a **read-only** `PageTree`, `Revision.h`, `Scope`, `RepairReport`. `Revision::next()` only goes up, so **undo yields a new revision** | Dirty tracking cannot be *revision == saved revision* — **D3-10** |
| `core/doc/RepairReport::requiresSaveAs()` | Built and tested (Stage 2 A-22), **deliberately not `!isEmpty()`**, and read by nothing | T8 is its first reader — **D3-6** |
| `core/render/RenderCache::invalidate(document, scope, revision)` | Drops what the scope names and **re-keys the rest** to the new revision (Stage 2 A-41). Never called by the product — nothing has ever changed a document | T8 is its first real caller, and the in-flight tile rendered at the old revision is the race to test |
| `core/render/RenderRequest::viewRotation` | The **view** rotation, keyed; `PdfiumRasterizer` passes it as `FPDF_RenderPageBitmap`'s `rotate` and PDFium applies the page's own `/Rotate` itself | An unsaved **page** rotation has no field to travel in — **D3-15** |
| `ui/shell/DocumentWindow` | Holds a handle, a path, a `Revision::initial()` that never changes, and a `PageView` | T10 gives it the journal |
| `ActionRegistry` | **361 declared, 48 registered, 313 not** (`rawpage.exe --list-actions`, re-measured 2026-09-14). `file.save`, `file.saveAs`, `edit.undo`, `edit.redo` and `page.rotate` are all declared and unregistered; `page.rotate` is bound in **both** `Viewer` and `PageThumbnails` | T10 registers exactly those five |
| **`tests/ui/tst_context.cpp`** | *"a chord whose command is not registered yet leaves the key alone"* **uses `Ctrl+S`**, and its comment says *"nothing registers it until Stage 3"* | T10 breaks it by construction and must move it to a chord still unregistered after Stage 3, keeping what it proves. The neighbouring case's comment — *"Stage 3 registers `edit.selectAll` for real"* — is a prediction nothing in Stage 3 fulfils, and is corrected in the same change (rule 4, `CLAUDE.md`) |
| The Viewer-subset case | Walks the **fourteen** §8 Viewer chords later stages own and requires each to stay `NotFound`; `Ctrl+R` is one of them (Stage 2 guide §6's list) | T10 takes `Ctrl+R` out of that list, and the list is re-derived from `KeyBindingDefaults.def` against `--list-actions` rather than edited by hand |
| `app/CrashHandler` | *"Stage 3 wires `RecoveryJournal::flushAll()` here"* — in `CrashHandler.h`, `main.cpp`, and the startup log line (`lastRites no-op (RecoveryJournal arrives at Stage 3)`) | **D3-11 decides against it**: with an fsync per command there is nothing to flush, and doing I/O in a crashed process is a hazard. T11 corrects the three sentences rather than fulfilling them |
| `tools/mutation-lint/` | Does not exist. `cmake/RawPageChecks.cmake` says *"THREE of the four gates exist"* | T6. From that task `checks` says **four**, and the sentence becomes false in the same change |
| `tests/fuzz/CMakeLists.txt` | `RAWPAGE_EXPECTED_FUZZ_TARGETS 2`; the ten-minute soak is `.github/workflows/fuzz-soak.yml` (run 34781013867, clean) | **D3-17** makes it 3 |
| `corpus/` | **61 generated files**, no signed document, no inherited `/Rotate`, and **no hybrid `/XRefStm` file** — `tests/unit/tst_xreftable.cpp`'s header said the path was exercised against a hand-built buffer and that *"the honest fix is a corpus file"*; **no such case existed, so the path had never run under test** (found at T1, guide issue **I-03**). `corpus/make-encrypted.py` and `corpus/PROVENANCE.md` both say *"signing is Stage 10"*; `Implementation-Plan.md` puts it at 11 | T1 — **D3-2** |
| `license-audit` | 20 components; the vcpkg `crypto` feature (OpenSSL) is declared and **off** | Stays off. Stage 3 links no cryptography — which is what makes the OpenSSL **command-line tool** an independent validator (D3-2) |
| `layer-lint` | 284 files, 2,056 includes, **8 live escapes**, re-measured 2026-09-14 with `--explain`. There is **no `pdf/cos/` layer entry**: `pdf/cos/` is matched by the `pdf` entry, which lets it include `pdf/render/` and, from this stage, `pdf/ops/` | **D3-3** adds the entry |
| Preferences | Five of 26 pages ship (General, Page Display, Performance, Logging, Shortcuts). `documents/recentCount` and `documents/recentPinned` are declared in the **Documents** group, which has no page | T12 ships the Documents page — six of 26 |
| Open issues carried | Stage 2's **I-06** and **I-11** (open by decision), **I-28** (Fit Visible) and **I-34** (second-level key tips), all S3 | None touches a Stage 3 task. Stage 3 starts with no open S1 or S2 |

---

## 3. Stage-3-local decisions

Choices this plan makes because nothing upstream decides them. Each is scoped to Stage 3 and listed
here so a later stage can find and revisit it rather than rediscover it.

| # | Decision | Why | Revisit |
|---|---|---|---|
| **D3-1** | **A4 is measured TWICE: a probe at T2 over the writer alone, and the product at T13 through the shipped save path.** The probe drives `pdf/cos/IncrementalWriter` with a hand-built delta — one page dictionary with a new `/Rotate` — over every signed corpus file, and asks the independent validators. The product run goes `RotatePageCommand` → `CommandJournal` → `MutationSinkImpl` → `IncrementalWriter` → `SaveCoordinator` → `platform::IDurableFiles`, and asks them again of the file on disk. **Both figures are recorded in `Tech-Stack-Plan.md` §3.3; the second is the claim about the product.** **Amended by the user 2026-09-14, after the first Acrobat walk (guide issue I-06):** Acrobat rejects a page rotation after these approval signatures from ANY writer — its policy permits only form fill-in, signing and commenting — while accepting our writer's update for a comment. So the probe writes **two** edits per file: a **comment**, on which Acrobat grades A4, and the **rotation**, kept as a control Acrobat must reject; OpenSSL judges integrity after both. The product can only rotate before Stage 6, so the product's Acrobat half after a permitted edit is carried to Stage 6's exit criteria, and T13's verdict records itself as partial. | D2-1's method, which Stage 2 T18 vindicated: its probe said 31× headroom and the product said 1.18×, and the difference was the product. Between the writer and the disk, Stage 3 puts a sink that decides *which* objects to write, a page-tree walk that decides *where* `/Rotate` goes, a temporary copy, and a replace — every one of which can move a byte the writer never touched. A probe answers *"can an appended update preserve a signature"* early enough to change the writer's design; only the product answers *"does RawPage's Save preserve it"*. | Never — the two-part method is the point |
| **D3-2** | **The signed corpus is generated like every other corpus file (D2-11), and A4 is judged by two validators that share no code with RawPage: the OpenSSL command-line tool (automated) and Adobe Acrobat (manual).** *Decided by the user 2026-09-14.* `corpus/make-signed.py` builds, in pure Python: an RSA-2048 key derived deterministically from a seed in the generator, a self-signed X.509 certificate whose subject says it is a corpus test identity, and a detached CMS `SignedData` with signed attributes over the file's `/ByteRange`. Four files: an approval signature over a classic-xref file; the same over an xref-stream file; a **certification signature with `/DocMDP /P 1`** — *no changes permitted* — which is the control (a validator that judges modification policy must flag it after a rotation); and an approval signature followed by an unsigned incremental update, so a save appends a **third** revision. Validation extracts `/ByteRange` and `/Contents` with a byte-level script that does not use `pdf/cos/`, and runs `openssl cms -verify` over them. | Three constraints, each already in the record. (1) **D2-11**: nothing in `corpus/` is borrowed, and every file regenerates byte-identically — which RSA PKCS#1 v1.5 permits, because it is deterministic, and which a fixed `signingTime` attribute preserves. (2) **`corpus/PROVENANCE.md` already says** *"a private key committed to a repository is a bad habit even when it protects nothing"*: no key file is committed; the key is re-derived at generation time, and the certificate names itself a test identity so nobody mistakes the output for a real signature. (3) **The validator must be independent, and the reason is the one P-4 gave for Stage 2's reference renderer**: a verifier written by the author of the writer agrees with the writer. OpenSSL shares nothing with this build — the `crypto` feature is off and nothing links it — and it is on this machine and on the Ubuntu runners. **What each validator can and cannot say is different, and the pair is the answer**: OpenSSL proves the *integrity* of the signed range and knows nothing about modification policy; Acrobat judges policy (`/DocMDP`, what later revisions did) and cannot be automated here. | **Stage 11**, when the product links OpenSSL for its own validator: the automated check then stops being independent of the product and needs a second engine beside it |
| **D3-3** | **`pdf/ops/` is a new PORTABLE static library, `rawpage_ops`, linking `rawpage_cos` and no engine; and `pdf/cos/` gets its own `layer-lint` entry forbidding it from including `pdf/ops/` or `pdf/render/`.** `rawpage_tests` links `rawpage_ops`; `rawpage_assert_headless()`'s forbidden list is unchanged, and a fixture proves `rawpage_ops` did not bring `rawpage_pdfium` with it. `pdf/DocumentBackendImpl` stays in `rawpage_pdfium` and **delegates** every mutation and every write to `rawpage_ops`. | The writer and the sink are the memory-unsafe code this stage adds — byte offsets, spans into a buffer, a new xref computed from old ones — and **`portable-asan` and `portable-fuzz` are the only sanitizer coverage this project has** (O10, `Implementation-Plan.md` §8). Placing `pdf/ops/` beside `DocumentBackendImpl` in `rawpage_pdfium` would have been the path of least resistance and would have put Stage 3's riskiest code exactly where ASan cannot reach it. The layer entry closes a gap that exists today and becomes a real one this stage: the `pdf` entry lets `pdf/cos/` include anything under `pdf/`, so the moment `pdf/ops/` exists a reader could start depending on the mutation layer above it, and only the linker would notice — late, and only for a call rather than a header. | Never |
| **D3-4** | **Save writes a complete new file beside the target and atomically replaces the target with it.** *Decided by the user 2026-09-14.* The sequence, owned by `core/doc/SaveCoordinator` so that the headless batch path (flow K) reaches it: (1) check the target is still the file that was opened (**D3-16**); (2) the backend writes **the opened bytes, copied verbatim, followed by the update** to a temporary file in the target's directory; (3) the backend **re-reads that file through the COS reader** and checks it before anything is replaced — the prefix is identical, the new revision parses, and every changed object reads back as the model says; (4) `platform::IDurableFiles` flushes it to disk; (5) `IDurableFiles` replaces the target keeping its creation time, security descriptor and **alternate data streams** — the `Zone.Identifier` stream that marks a downloaded file is one of them — by carrying those onto the temporary file and then making **one atomic rename over the target**, so that no instant exists at which the target's path is empty. **Corrected 2026-09-27** (`Stage4-ViewingSurface-Testing-Guide.md` §8, **I-31**): T7 built step 5 on **`ReplaceFileW`**, which is two renames inside the calling process, and a process killed between them left NO file at the target — the document under a backup name. The decision stands; its mechanism was wrong, and `platform/win/WinDurableFiles.h` records the replacement and what it does not carry; (6) only then does the journal mark the revision saved. **`platform::IDurableFiles` is the eighteenth `platform/platform.h` interface.** A failure at any step leaves the target untouched and the temporary file deleted. | The alternative was to append in place: never physically rewrite the original, at the cost of a torn tail if the process dies mid-append. **The user chose the version with no torn state**, and the costs are recorded rather than absorbed. Every save now writes every byte of the file, so a save costs I/O proportional to the file rather than to the edit — T8 measures it on `corpus/ordinary/pages-900.pdf` and on a generated 200 MB file. `Tech-Stack-Plan.md` §5.4's *"the original bytes are never rewritten"* is refined, not contradicted: they are never **re-serialized** — they are copied — and A4's property is byte identity, which is asserted on the replaced file on disk. **Why not `QSaveFile`:** `QSaveFile` renames over the target, and a rename gives the result the temporary file's streams and creation time, silently discarding the mark that tells Windows a PDF came from the internet. (T7's answer was `ReplaceFileW`; since I-31 it is a rename that happens only after the identity has been carried onto the file being renamed.) **Why an interface**: `Tech-Stack-Plan.md` §4.3's rule is that what Qt does cross-platform gets no interface, and Qt provides neither a public flush-to-disk on a file nor a replace that keeps streams — both are exactly §4.3's *"needs platform code"* column. **Why the re-read comes before the replace**: a writer whose output its own reader cannot parse is a bug that must never reach the only copy of a user's document, and reading it back afterwards would be finding out too late. **Why `SaveCoordinator` is in `core/`**: the decision of *whether* to save, *where*, and *what to do when it fails* is domain policy, and `core/` reaches both the backend and the platform through interfaces without writing a byte itself | Stage 8, when `FullWriter` adds the second writer behind the same coordinator; and if T8's measurement makes Save on a large file unacceptable |
| **D3-5** | **An update contains exactly the objects whose desired state differs from their state in the NEWEST revision on disk — and nothing else.** `MutationSinkImpl` holds desired state, not a log: a rotation undone before Save writes **no object and no update at all**, and a rotation saved and then undone writes the page dictionary back to its pre-rotation value in a new revision. `PageOps` writes `/Rotate` on the **page dictionary itself**, never on an ancestor `/Pages` node (which would rotate its siblings), and **removes a `/Rotate` it would otherwise write equal to the inherited value** when the on-disk dictionary had none. *(Refined at T3: **whenever** it equals the inherited value, whatever the on-disk dictionary held — after a save and an undo the literal rule would leave an explicit `/Rotate` on a page whose original dictionary had none, and the refinement writes that original back. Recorded in `pdf/ops/PageOps.h` and the T3 row of `Implementation-Plan.md` §4.)* The update's cross-reference section **matches the kind of the newest section**: a classic table after a classic table, an xref stream after an xref stream (its own object number from `ObjectAllocator`), and a classic table after a hybrid `/XRefStm` file. **A new object number is allocated at or above the newest `/Size`; an incremental update never reuses a free-list entry.** **A changed object that lived inside an `/ObjStm` is written as an ordinary object under its own number**, whose type-1 entry supersedes the old type-2 one; the object stream is not rewritten. The trailer carries `/Prev`, the merged keys, `/Size` = max of the old and new, and an `/ID` whose first element is kept and whose second is derived from a hash of the update — **and, after a hybrid file, the newest trailer's `/XRefStm` unchanged** *(amended 2026-09-17, Stage 3 guide I-14: Acrobat reads `/XRefStm` only from the newest trailer, and without it reported every saved hybrid file damaged — M-02's walk)*. **`/Info` and XMP metadata are not touched.** An untouched object is never copied into the update. | This is what *"untouched objects are byte-identical"* has to mean for the claim to be checkable: an object not in the update is not a *copy* of the original, it is the original, at its original offset, reached through `/Prev`. Computing edits against the **newest** revision rather than the file as first opened is what makes undo-after-save correct, and computing them against **desired state** is what makes undo-before-save write nothing — the reversibility property test's strongest form. The inherited-`/Rotate` rule is the off-by-a-sibling bug; `corpus/ordinary/rotate-inherited.pdf` (T1) exists for it. Matching the section kind avoids a hybrid that some readers reject. A deterministic `/ID` makes the writer's output reproducible, so an expected update can be a committed fixture. Never reusing a freed number is the conservative reading of ISO 32000-1 7.5.4: reuse with an incremented generation is legal, and a reader that caches by number alone gets it wrong, while a fresh number costs one xref entry. **Leaving `/Info` alone is a scope line, not an oversight**: updating `/ModDate` rewrites a second object on every save and — through XMP — a stream, and the metadata model is Stage 4's (D3-8) | Stage 4 or Stage 8, when a metadata model can decide `/ModDate` policy |
| **D3-6** | **A repaired document's first save is a forced Save-As whose update carries a COMPLETE cross-reference section and no `/Prev`.** `RepairReport::requiresSaveAs()` decides; Save becomes Save-As with an explanation first. The new file is the original bytes, copied verbatim, plus an update whose table lists **every** object at the offset `XrefRepair` verified, so the new file opens **without repair**. `documents/warnOnRepairedSave` controls whether the explanation is shown; it can never turn the forcing off. | Appending an ordinary update to a repaired file chains `/Prev` into the table that was broken, so every reader has to repair the result again — and the product would have saved a file that still says *"repaired"* on reopen. The complete table is what makes the Save-As worth forcing. It keeps the original bytes, damage included, which is the conservative choice: nothing is discarded that a later, better repair might want. The standing rule (`CLAUDE.md`) is *"a repaired document's first save is forced to Save-As"*, and a setting that disabled it would make the rule a preference | Stage 8, when `FullWriter` can offer a rewritten repair instead |
| **D3-7** | **`core/doc/Permissions` comes forward from Stage 10 as the PRE-FLIGHT HALF: `may(Operation)` answers whether this document can be edited AT ALL in this build, and why not.** Stage 3's refusals, each with its own catalog key: the document is **encrypted** (the COS reader declines it, D2-6, so there is no object model to write through); the **COS reader and the engine disagree** about the page count, the page order or a page's rotation; the COS reader **could not build a page-object map** at all. The **permission bits** of an encrypted document stay Stage 10's. The row's tag becomes **○ (S3 pre-flight, S10 permission bits)**. | `CommandJournal::run` asks `Permissions::may()` first, per `Implementation-Plan.md` § Stage 3, and `tst_journal`'s *"permission refusal leaves no trace"* needs something to refuse. Building the seam now means Stage 10 adds bits to a predicate every command already asks, rather than threading a new check through every command. **Refusing up front rather than at Save** is the point: a user who rotates twenty pages of an encrypted file and is then told it cannot be saved has lost the work by the product's design. **The disagreement refusal is D2-6's argument arriving from the other side**: the rotation is applied to an object the COS reader chose and rendered by an engine that chose its own, and if they chose different pages the product would save a rotation onto a page nobody saw. The split tag follows `PageTree`'s **○ (S2 read, S3 edit)** precedent | Stage 10 adds the permission bits and encrypted editing |
| **D3-8** | **`core/doc/` gains only what a Stage 3 task calls: `Document`, `DocumentId`, `Permissions` (pre-flight) and `PageTree`'s ROTATION. `Outline` and `Metadata` move to Stage 4, and `PageTree`'s insert, move, remove and duplicate move to Stage 7.** *Outline and Metadata decided by the user 2026-09-14*; the `PageTree` half follows from the same rule. | D2-5 brought four types forward because Stage 2 signatures already named them. This is the same argument in reverse: nothing in Stage 3 reads an outline or metadata — their first consumers, `BookmarksPane` and `DocumentPropertiesDialog`, are both in `Implementation-Plan.md` § Stage 4 — and no Stage 3 command inserts, moves or removes a page. A model type with no caller is tested only against its own idea of what a caller wants, and Stage 1's I-38 (`general/touchMode`, declared and read by nothing) is what that shape costs. `PageTree`'s row already records the principle — its mutating half is *"absent rather than present-and-refusing"* | Stage 4 (`Outline` read, `Metadata`), Stage 7 (page-tree edits, with their commands) |
| **D3-9** | **Stage 3 declares two settings keys — `documents/recoveryDir` and `documents/warnOnRepairedSave` — not four.** `documents/saveMode` moves to Stage 8 and `documents/autosaveIntervalSec` to Stage 13. *Decided by the user 2026-09-14.* The Documents Preferences page ships (convention 17). | `saveMode = full` needs `FullWriter`, which is Stage 8's. `autosaveIntervalSec` has no meaning once the recovery journal is flushed per command (`Tech-Stack-Plan.md` §5.4) — there is no interval to configure; where an interval does mean something is coalescing a burst of Write-mode typing into one journal line, which is Stage 13. Declaring either now would ship a control that reads back as set and does nothing, which is Stage 2's I-11 by construction rather than by constraint | Stage 8, Stage 13 |
| **D3-10** | **`CommandJournal` is the only holder of a mutable `Document`.** `ui/shell/DocumentWindow` owns the journal and receives `const Document&`, so *"`ui/` may never mutate a `Document`"* is a property of the types rather than of review. Three consequences, each fixing a signature the document set wrote before it could be built: **(a)** `Command::name()` returns a **catalog key and arguments** (`command.page.rotate`), never a translated `QString` — `core/` has no `tr()` (convention 5; guide issue **I-01**); **(b)** `mutated` and `dirtyChanged` are **subscription tokens**, the pattern `core/settings/Settings` already uses, because `core/` has no `Signal` type and this is its second user rather than a reason to invent one; **(c)** **dirty is *the journal's position differs from the saved position***, not *revision differs from the saved revision*, because `Revision::next()` only goes up and an undo back to the saved state is a new revision that is not dirty. | Each is a place where the design as written would either not compile under the layer rules or would be wrong on its second use. (c) is the subtle one: a revision-equality dirty flag reports *"unsaved changes"* after rotate → save → rotate → undo, and the user is asked to save a file that is identical to the one on disk. The saved position is a marker in the undo history, so it is also correctly **unreachable** after rotate → save → undo → rotate-a-different-page, where no amount of undoing returns to the saved state | Never |
| **D3-11** | **The recovery journal records OPERATIONS, and the unit of replay is the UNDO STEP.** `journal.ndjson` holds one line per operation — `run`, `undo`, `redo`, `groupBegin`, `groupEnd` — and **every `undo` and `redo` line carries the full serialized command**, so replay never depends on a stack it did not rebuild. **A command is recoverable once `run()` has returned**: the line is flushed to disk before `run()` returns, so a kill can lose at most the command still inside `run()`, never an earlier one. **A group is one undo step and therefore one recovery step**: an unterminated trailing group is discarded and reported, and a failure at step *k* keeps steps 1…*k*−1 and names *k*. Recovery restores the document and **the undo history since the last save**; history before the last save is not recovered, and an `undo` line naming a command from before it is applied as that command's `revert()`. Each open document gets its own directory, `<DocumentId>-<pid>-<n>`, **locked while its owner lives**, so discovery never offers a running instance's journal as a crashed one. When the recovery directory cannot be written, or `IDurableFiles` is null, the journal **degrades to memory and says so once** (`Tech-Stack-Plan.md` §5.5). When the source file no longer matches `meta.json`, *"recover to a new file"* replays onto the **current** source into an **untitled** document that must be saved with Save-As. The journal's file I/O is in `core/cmd/RecoveryJournal.cpp` behind named `layer-lint` waivers — `core/render/DiskTileStore`'s precedent, with the directory chosen by `app/` — and its flush goes through `IDurableFiles`. **`app/CrashHandler`'s last rites stay I/O-free.** | `Implementation-Plan.md` says *"replay is per-command resumable"*, and for a single command it is. For a group it would be wrong: *"rotate all pages"* is one user action and one `Ctrl+Z`, and recovering nine of its twelve rotations puts a document on screen that the user never produced. **Carrying the command on the undo line** is what makes an undo after a save recoverable at all — the command it undoes is in the saved file, not in the journal. The per-open directory and the lock are the two ways one journal file could otherwise be written by two owners: the same file open in two windows, and two instances with `general/singleInstance` off. **Why the last rites do nothing**: they were written to flush a buffered journal, and D3-11's journal has no buffer; the recovery claim rests on the flush inside `run()`, and a case kills the process with `TerminateProcess` — which runs no crash handler at all — to prove it | Stage 13, when typing is coalesced and a line is no longer written per keystroke |
| **D3-12** | **`mutation-lint` reads a declarative table and Catch2 tags, as text, with the standard library only.** `core/cmd/Commands.def` is an X-macro table — one `RAWPAGE_COMMAND(Class, "type.id", "header")` row per command — from which `CommandRegistry` is generated. The gate finds every class deriving from `Command` under `core/cmd/commands/`, and fails when one has **no table row**, **no test case tagged `[serialize:Class]`**, or **no test case tagged `[revert:Class]`** under `tests/`. It runs on the bare `gates` preset. `checks` then runs and **says four**. **A command whose `revert()` exists and lies is NOT the gate's to catch** — it is `tst_reversibility`'s, and the guide carries a fixture for each. | `ActionId.def` and `SettingsKeys.def` are the established shape (D1-6, convention 17): a table is what a tool can read without compiling, and what makes a missing row a build error on the line that is missing. Tags rather than test *names* because a name carries free text and convention 2 forbids a comma in it; a tag is a token. **The gate proves a test exists; it cannot prove the test tests anything** — which is why the exit criterion's *"fails on a test command with no inverse"* is met by two mechanisms, and the guide says which fixture each one fires on | Never |
| **D3-13** | **`Ctrl+R` opens a Rotate Pages dialog, and the dialog is `PageOpsDialogs`' rotate half brought forward from Stage 7.** It collects a direction (90° clockwise, 180°, 90° counter-clockwise) and a page set (**the current page** or **all pages**) and constructs one `RotatePageCommand` per page inside one group. Ranges, odd/even and orientation filters stay Stage 7's. The **`PageThumbnails` binding** resolves to the same command, and is asserted through a test widget that declares that context, because **`ThumbnailsPane` is Stage 4's** — the case says at its assertion that no pane gesture is covered (convention 19). | `Features.md` §8 is authoritative and says `Ctrl+R` *"opens the dialog"*; a chord that rotated immediately would be a Stage 3 redefinition of a binding this plan does not own. A two-field dialog is also the first **production** caller of `beginGroup`/`endGroup` — without *"all pages"* the grouping would be tested only by tests. `Implementation-Plan.md` names `Ctrl+R` in the Thumbnails context as *"the first chord to demonstrate context dispatch against a real mutation"*, and the context can be demonstrated; the pane cannot, and saying so is better than a stub pane | Stage 4 (the pane), Stage 7 (the rest of the dialog) |
| **D3-14** | **A document with unsaved changes cannot be closed without a disposition — Save, Discard or Cancel.** Closing a tab, closing a window, tearing a tab out and closing the new window, and `Ctrl+Q` all ask, per dirty document. **Discard deletes the recovery directory; Cancel leaves everything as it was.** A headless close of a dirty document is refused with `Unavailable` unless the caller passes a disposition (convention 16). | Not in `Implementation-Plan.md`'s component list, and Stage 3 cannot ship without it: the first stage that can change a document is the first stage in which closing one can lose work, and a recovery journal that is deleted on a normal close would make that loss silent. It is a dialog that collects a parameter and nothing else, so it breaks no standing rule | Never |
| **D3-15** | **An unsaved rotation is DRAWN by composing it into the render request; the engine is never mutated and never reloaded at Stage 3.** `RenderRequest` gains a keyed per-page **rotation delta** — the model's rotation minus the rotation the engine loaded — and the rasterizer passes delta plus view rotation to `FPDF_RenderPageBitmap`. Layout, the status bar and every other reader of page geometry read `Document`, never `IDocumentBackend::pages()`, after hydration. After a Save the engine keeps the bytes it loaded, and the delta stays relative to them. | *"We read through PDFium and write through our own code"*: an `FPDFPage_SetRotation` would make PDFium's in-memory model a second, unsaved copy of the document that nothing journals and nothing can revert. A rotation is the one mutation a render parameter can express exactly, so it costs nothing to keep the engine read-only. **The trap is the one `PdfiumRasterizer.cpp` already warns about** — passing a combined rotation where PDFium already applies `/Rotate` rotates a page twice — so the field carries a delta and a case renders the saved file and the unsaved delta and requires the same pixels. Reloading the engine after each Save would drop its document while tiles are in flight, for no visible benefit | **Stage 6**, the first mutation a render parameter cannot express (an annotation's appearance) -- **revisited at Stage 6 T4, 2026-10-05: [`Stage6-Annotations-Plan.md`](./Stage6-Annotations-Plan.md) D6-2** -- an unsaved edit is drawn by the engine RELOADING the composed revision (the bytes Save would write); a rotation keeps its delta, which the rasterizer corrects for a reloaded `/Rotate` |
| **D3-16** | **Before any save, `SaveCoordinator` confirms the target is still the file that was opened, by `DocumentId`** — path, size, modification time and a content hash taken at open. A mismatch forces Save-As with an explanation; a target that is read-only, locked by another process or in a directory that cannot hold the temporary file fails with a named error and offers Save-As. **There is no fallback to writing the target directly.** `file.saveAs` is registered at Stage 3 and writes through the same path to the chosen file. | Appending the update computed against bytes that are no longer on disk would produce a file whose `/Prev` points into someone else's content. The hash is a real cost on a large file and T8 measures it rather than assuming it is free. Refusing a direct write is the whole of D3-4's guarantee: a fallback that writes in place when the directory is not writable is a torn file by another route — `QSaveFile::setDirectWriteFallback` is exactly that switch, and the reason this design does not use it. **Registering Save-As now**, with the incremental writer behind it, is `Implementation-Plan.md`'s own deferral note (*"this stage's forced-Save-As path writes through the incremental writer to a fresh file"*); a Save-As reachable only when forced would be the path nothing else exercises | Stage 8 changes what Save-As writes; the coordinator and the binding stay |
| **D3-17** | **A third fuzz target, `fuzz_cos_roundtrip`: parse any input; if it is editable, rotate its first page, write the update, re-parse; require the prefix unchanged, the re-parse to succeed, and the rotation to read back.** `RAWPAGE_EXPECTED_FUZZ_TARGETS` becomes **3**; the soak workflow runs all three. Seeds gain the writer's own outputs. | `Implementation-Plan.md` § Stage 3 says *"Stage 3 adds seeds, not targets"*, and that line was written when the two COS targets moved to Stage 2 — about **where they live**, not about the writer. The writer is the riskiest first-party code in the project and consumes whatever the tolerant reader produced from a malformed file: spans that stop short, objects recovered by scan, lengths that lied. A property that *"whatever we could read, we can append to and read again"* is what a fuzzer is for, and it is the first target that tests two components against each other. The line is corrected in the same change as this plan | Never |
| **D3-18** | **`tests/roundtrip/` is a source directory compiled into `rawpage_tests`, not a sixth test target**; the half that re-reads a saved file **through PDFium** is `tests/render/tst_saveengine.cpp`, and the half that exercises the real flush and replace is `tests/platform/`. | `Implementation-Plan.md` §2.1: a suite is a separate target when linking something would destroy a property another suite proves. The byte-level round trip links `rawpage_core`, `rawpage_cos` and `rawpage_ops` — exactly what `rawpage_tests` already may — so a sixth target would be organization, and the rule says organization is not a reason. **It also runs the round trip under `portable-asan`**, which a Windows-only target would not. The engine re-read needs the engine and so lives where the engine is linked | Never |

---

## 4. File manifest

Every file Stage 3 creates or turns from ○, with the [`Architecture-FileMap.md`](./Architecture-FileMap.md)
row it must flip **in the same change**. Rows marked ⚠️ have no FileMap row yet and are lockstep debt
(§10), paid with the file.

| File | FileMap row | Task |
|---|---|---|
| `corpus/make-signed.py`, `corpus/cmsenc.py` | ⚠️ §3.18 — two rows, beside `cryptenc.py` | T1 |
| `corpus/signed/approval.pdf`, `approval-xrefstream.pdf`, `certified-nochanges.pdf`, `approval-then-update.pdf` | ⚠️ §3.18 — one directory row, as `encrypted/` has; four `PROVENANCE.md` rows | T1 |
| `corpus/ordinary/rotate-inherited.pdf`, `corpus/ordinary/hybrid-xrefstm.pdf` | §3.18's `ordinary/` row; two `PROVENANCE.md` rows | T1 |
| `pdf/cos/ObjectAllocator.{h,cpp}`, `pdf/cos/IncrementalWriter.{h,cpp}` | §3.9 ○ (S3) → ✅ | T2 |
| `pdf/ops/PageOps.{h,cpp}` | §3.11 ○ (S3 rotate, S7 rest) → 🟡 (S3 rotate ✅, S7 rest) | T3 |
| `pdf/ops/MutationSinkImpl.{h,cpp}` | §3.11 ○ (S3) → ✅ | T3 |
| `pdf/ops/CMakeLists.txt` (`rawpage_ops`) | §3.18 build rows; ⚠️ the library list in `Implementation-Plan.md` §2.1 and `CLAUDE.md`'s architecture paragraph | T3 |
| `core/doc/Document.{h,cpp}`, `core/doc/DocumentId.h` | §3.2 ○ (S3) → ✅ | T4 |
| `core/doc/Permissions.{h,cpp}` | §3.2 ○ (S10) → 🟡 **(S3 pre-flight ✅, S10 permission bits)** (D3-7) | T4 |
| `core/doc/PageTree.{h,cpp}` — rotation | §3.2 ✅ (S2 read) / ○ (S3 edit) → ✅ (S2 read, **S3 rotate**) / ○ (**S7** insert, move, remove, duplicate) (D3-8) | T4 |
| `core/doc/Outline`, `core/doc/Metadata` | §3.2 — **re-tagged ○ (S4 read, S7 edit) and ○ (S4)** when this plan was written (D3-8) | — |
| `core/cmd/Command.h`, `core/cmd/Commands.def`, `core/cmd/CommandRegistry.{h,cpp}`, `core/cmd/CommandJournal.{h,cpp}` | §3.3 ○ (S3) → ✅; ⚠️ `Commands.def` has no row and joins `CommandRegistry`'s | T5 |
| `core/cmd/commands/PageCommands.{h,cpp}` | §3.3 ○ (S3 rotate, S7 rest) → 🟡 (S3 rotate ✅) | T5 |
| `tools/mutation-lint/`, `tests/gates/fixtures/mutation-lint/` | §3.17 ○ (S3) → ✅; the fixtures row gains the new directory | T6 |
| `platform/platform.h` — `IDurableFiles` | §3.12's header row: **seventeen → eighteen**, and `Tech-Stack-Plan.md` §4.3 gains a row | T7 |
| `platform/win/WinDurableFiles.{h,cpp}` | ⚠️ §3.12 — a new row | T7 |
| `core/doc/SaveCoordinator.{h,cpp}` | ⚠️ §3.2 — a new row (D3-4) | T8 |
| `pdf/DocumentBackendImpl.{h,cpp}` — `mutationSink()`, the save method, hydration | §3.11's row: implemented set **2 → 3 of 7** | T8 |
| `core/render/RenderRequest.h` — the rotation delta | §3.4's row (D3-15) | T8 |
| `tests/roundtrip/` | §3.17 ○ (S3) → ✅, with **D3-18**'s arrangement stated in the row | T8 |
| `tests/render/tst_saveengine.cpp` | §3.17's `tests/render/` row | T8 |
| `tests/bench/bench_save.cpp` (`rawpage_bench_save`) | §3.17's `tests/bench/` row — D3-4's and D3-16's cost figures; it reports and never fails on speed | T8 |
| `core/cmd/RecoveryJournal.{h,cpp}` | §3.3 ○ (S3) → ✅ | T9 |
| `tests/platform/killprobe/` (`rawpage_killprobe`) | ⚠️ §3.17 — a new row; a helper executable, not a test target | T9 |
| `ui/dialogs/RotatePagesDialog.{h,cpp}` | §3.15's dialog-family row: `PageOpsDialogs` becomes **S3 rotate, S7 rest** (D3-13) | T10 |
| `ui/dialogs/UnsavedChangesDialog.{h,cpp}` | ⚠️ §3.15 — named in the family row (D3-14) | T10 |
| `ui/shell/DocumentWindow`, `MainWindow`, `StatusBar`, `MessageBar` — journal, dirty marker, close disposition, new banner kinds | §3.13's rows | T10 |
| `app/RecoveryPrompt.{h,cpp}`, `ui/dialogs/RecoveryDialog.{h,cpp}` | §3.1 ○ (S3) → ✅; §3.15's family row | T11 |
| `app/CrashHandler.{h,cpp}`, `app/main.cpp` — the last-rites sentences | §3.1's row: *"with a no-op body until `RecoveryJournal` arrives at S3"* is corrected to D3-11's decision | T11 |
| `tests/fuzz/fuzz_cos_roundtrip.cpp` | §3.17's `tests/fuzz/` row: two targets → three | T13 |

**Settings keys** — **two** (D3-9), each a row in `core/settings/SettingsKeys.def` and therefore an
automatic editor (convention 17): `documents/recoveryDir` and `documents/warnOnRepairedSave`. The
Documents page ships — **six of 26**.

**Catalog families** — `i18n-lint`'s `keyFamilies` gains **`command`** (undo labels, D3-10) and
**`recovery`** (the recovery dialog and banners). Save's messages live in the existing `shell.` and
`error.` families.

---

## 5. Component design

Only the pieces whose shape is not obvious from a one-line component entry.

### 5.1 The library graph after Stage 3

```
rawpage_core        core/ …                          portable, no Qt GUI, no engine
rawpage_cos         pdf/cos/ + pdf/BackendFactory.h  portable, first-party reader AND WRITER
rawpage_ops         pdf/ops/                         portable, links rawpage_cos           ← NEW (D3-3)
rawpage_pdfium      pdf/render/ + pdf/DocumentBackendImpl   Windows, links pdfium.dll and rawpage_ops
rawpage_platform    platform/win/                    Windows
rawpage_ui          ui/                              Qt Widgets
```

`rawpage_tests` links `rawpage_core` + `rawpage_cos` + **`rawpage_ops`**, under the unchanged
`rawpage_assert_headless()`. `layer-lint` gains a **`pdf-cos`** entry (`mayInclude`: `pdf/cos/`,
`core/`), with a fixture that includes `pdf/ops/` from `pdf/cos/` and a control that includes
`pdf/cos/` from `pdf/ops/`.

### 5.2 One mutation, as it will actually run

Flow D, with this plan's decisions written in:

```
Ctrl+R in the viewer ─▶ ShortcutBinder ─▶ page.rotate (Viewer)
 └─ handler resolves MainWindow::activeShell() at DISPATCH time        (convention 14)
     └─ RotatePagesDialog collects {direction, current|all}            (D3-13) ── ui/ STOPS at journal.run
         └─ journal.beginGroup("command.page.rotateMany")               only for "all pages"
             └─ journal.run(RotatePageCommand{page, +90})
                 ├─ Permissions::may(EditPages)? no ─▶ refuse by key, nothing changed      (D3-7)
                 ├─ apply(Document&, MutationSink&)
                 │    ├─ PageTree.setRotation(page, r+90)               core model
                 │    └─ sink.setPageRotation(page, r+90)               INTENT: desired state (D3-5)
                 ├─ RecoveryJournal: {"op":"run","cmd":{…}} + flush     before run() returns (D3-11)
                 ├─ position++ ; revision = revision.next()
                 └─ notify mutated(Scope::page(p)) ; dirtyChanged(position != saved)       (D3-10)
                      ├─ RenderCache.invalidate(doc, page p, revision)  drops p, re-keys the rest
                      ├─ PageLayoutEngine re-lays out                   p's display size may have swapped
                      └─ requests for p carry rotationDelta = model − engine-at-load       (D3-15)
         └─ journal.endGroup()
```

### 5.3 The save transaction

```
Ctrl+S ─▶ file.save ─▶ SaveCoordinator.save(document)                      core/doc (D3-4)
 ├─ journal clean?                     ─▶ nothing to do (the command is disabled, and says why)
 ├─ repairReport.requiresSaveAs()?     ─▶ explanation (warnOnRepairedSave) ─▶ Save-As   (D3-6)
 ├─ DocumentId still matches the target? no ─▶ explanation ─▶ Save-As                   (D3-16)
 ├─ backend.writeRevision(handle, <target dir>/.<name>.rawpage-<pid>.tmp)
 │    ├─ copy the opened bytes VERBATIM
 │    ├─ IncrementalWriter.append(device, sink.delta())   classic | stream | complete (D3-5, D3-6)
 │    └─ re-read the temporary file through pdf/cos: prefix identical, revision parses,
 │         every changed object reads back                 ─▶ else delete it, fail by key
 ├─ durableFiles.syncToDisk(temp)
 ├─ durableFiles.replaceFile(target, temp)                 streams, ACL, times kept -- since S4 I-31 by
 │                                                         carrying them, then ONE atomic rename (was ReplaceFileW)
 ├─ backend.adoptRevision(handle, target)                  newest revision = what is now on disk
 └─ journal.markSaved() ─▶ dirtyChanged(false) ─▶ RecoveryJournal rewrites meta.json, truncates lines
```

**Every failure before `replaceFile` leaves the target untouched and removes the temporary file.**
A kill before `replaceFile` leaves a stray temporary file and the original; a kill after leaves the
new file. There is no instant at which the target is partial — which is the property the kill harness
checks (guide §5.10), not a property it assumes.

### 5.4 `CommandJournal` — the three numbers it keeps

| Number | Moves on | Used for |
|---|---|---|
| **`Revision`** | every successful `run`, `undo`, `redo` — always upward | cache keys and staleness; never for *dirty* |
| **position** | `run` +1 (and truncates redo), `undo` −1, `redo` +1 | where in the history the document is |
| **saved position** | `markSaved()` sets it to *position*; a `run` that truncates redo past it makes it **unreachable** | `isDirty() == (position != savedPosition)` |

A group is one entry in the history. `beginGroup` inside a group nests into the outer one;
`endGroup` with nothing applied inside removes the empty entry rather than recording a no-op step.

### 5.5 The recovery directory

```
%LOCALAPPDATA%\RawPage\recovery\            ← documents/recoveryDir, when set
  <DocumentId>-<pid>-<n>\
    lock                ← QLockFile; a live owner's directory is never offered
    meta.json           ← source path, size, mtime, content hash, the saved revision; replaced atomically
    journal.ndjson      ← one operation per line, flushed per line
```

**`journal.ndjson` line grammar** (versioned by a first line, `{"format":1}`):
`{"op":"run","cmd":<serialized>}` · `{"op":"undo","cmd":<serialized>}` ·
`{"op":"redo","cmd":<serialized>}` · `{"op":"groupBegin","name":"<catalog key>"}` ·
`{"op":"groupEnd"}`. A trailing line with no newline is a **torn line**: it is ignored and reported,
and is the only kind of damage replay treats as expected. A malformed line anywhere else stops replay
at the step containing it.

### 5.6 Test targets after Stage 3

| Target | Links | Stage 3 suites | Runs where |
|---|---|---|---|
| `rawpage_tests` | `rawpage_core`, `rawpage_cos`, **`rawpage_ops`** | `tst_objectallocator`, `tst_incrementalwriter`, `tst_pageops`, `tst_mutationsink`, `tst_document`, `tst_permissions`, `tst_pagetree` (rotation), `tst_journal`, `tst_command_serialize`, `tst_reversibility`, `tst_savecoordinator`, `tst_recovery`, and `tests/roundtrip/` (D3-18) | Windows + `portable-asan` |
| `rawpage_render_tests` | **+ `rawpage_ops`** (T3) | + `tst_pageopsengine` (T3, guide A-29 — the page walk against the engine), + `tst_saveengine` — PDFium re-reads what we wrote; the unsaved delta and the saved file draw the same pixels | Windows |
| `rawpage_platform_tests` | unchanged, **plus a dependency on `rawpage_killprobe`** | + `tst_durablefiles`, + the kill cases | Windows |
| `rawpage_ui_tests` | unchanged | + `tst_mutationcommands`, `tst_unsavedchanges`, `tst_recoverydialog` | Windows, `offscreen` |
| `tests/fuzz/` | `rawpage_cos`, **`rawpage_ops`** | + `fuzz_cos_roundtrip` (D3-17) | `portable-fuzz` |
| `gates.` | no binary | + `mutation-lint` fixtures and control; + the `pdf-cos` layer fixture and control; + the signed-corpus verification and its flipped-byte control | `gates` preset |

---

## 6. Task breakdown

Ordering is risk-first. **T2 comes before any model, journal or UI code**: a no-go on A4 reshapes the
writer, and learning it after T10 means rebuilding the save path under a finished UI. Stage 2 put its
go/no-go third for the same reason.

| # | Task | Depends on | Content | Done when |
|---|---|---|---|---|
| **T1** | **The writer's corpus** | — | `corpus/make-signed.py` and `corpus/cmsenc.py` (D3-2); the four `signed/` files; `ordinary/rotate-inherited.pdf` (`/Rotate 90` on a `/Pages` node, one kid overriding with `/Rotate 0`, one inheriting); `ordinary/hybrid-xrefstm.pdf`; `PROVENANCE.md` rows; the *"signing is Stage 10"* sentence corrected to 11 in both places; `golden/manifest.json` rows for the new files | `make-corpus.py --verify`, `--check-provenance` and `--self-test` green; **`openssl cms -verify` accepts all four signatures, and rejects each with one byte flipped inside its signed range** (the control, run first); `golden-update --check-manifest` green |
| **T2** | **`ObjectAllocator`, `IncrementalWriter`, and the A4 PROBE** | T1 | The writer (§5.3's append, D3-5's three section kinds, D3-6's complete section); `tst_objectallocator`, `tst_incrementalwriter`; a hand-built delta applied to every signed file | Every update re-reads through `pdf/cos/XrefTable` with the change present and **the file up to the old `startxref` byte-identical**; **A4 PROBE VERDICT RECORDED** in `Tech-Stack-Plan.md` §3.3 — OpenSSL over all four files, the flipped-byte control red, and the user's Acrobat check of the probe outputs (guide M-03's steps, run early). **A no-go stops the stage** and reopens the writer's design before T3 |
| **T3** | `rawpage_ops`: `PageOps`, `MutationSinkImpl` | T2 | D3-3's library and `layer-lint` entry; the page-object walk (loop-safe, `/Count` distrusted, as `XrefRepair` does); D3-5's desired-state sink and inherited-`/Rotate` rule | `tst_pageops`, `tst_mutationsink` green, **including the controls**: undo-before-save produces an empty delta; rotating one kid of `rotate-inherited.pdf` changes no other object; the `pdf-cos` fixture fails and its control passes |
| **T4** | `core/doc/`: `Document`, `DocumentId`, `Permissions`, rotation | T3 | D3-7, D3-8; `Document::hydrate(IDocumentBackend&)`; the backend's COS/engine agreement check; `mutationSink()` non-null | `tst_document`, `tst_permissions` green; **every encrypted corpus file refuses editing by its catalog key**; every ordinary file is editable; a planted page-count disagreement refuses; the backend's census logs **3 of 7** |
| **T5** | `Command`, `Commands.def`, `CommandRegistry`, `CommandJournal`, `RotatePageCommand` | T4 | D3-10; §5.4's three numbers; groups; subscriptions | `tst_journal`, `tst_command_serialize`, **`tst_reversibility` over every editable corpus file** green — apply then revert restores the model hash **and an empty sink delta**; a planted command with a lying `revert()` fails it |
| **T6** | `tools/mutation-lint` | T5 | D3-12; fixtures for *no row*, *no serialize tag*, *no revert tag*, and a complete control | Each fixture fails naming its rule, the control passes, the real tree passes; **`checks` says four**, and `cmake/RawPageChecks.cmake`'s *"THREE of the four"* is gone in the same change |
| **T7** | `platform::IDurableFiles`, `WinDurableFiles` | — | The eighteenth interface; `ReplaceFileW` with a `MoveFileExW(MOVEFILE_WRITE_THROUGH)` path when the target does not exist; `ReplaceFileW(` and `MoveFileExW(` added to `layer-lint`'s `filesystem-write` patterns, with waivers on the implementation | `tst_durablefiles` green: a replaced file **keeps a planted `Zone.Identifier` stream** and its creation time; a locked target fails by key and leaves both files; the interface census reads **eighteen**, and `Tech-Stack-Plan.md` §4.3 has the row |
| **T8** | **The save path** | T5, T7 | `SaveCoordinator` (D3-4, D3-6, D3-16); the backend's write, re-read and adopt; D3-15's render delta; `tests/roundtrip/`; `tst_saveengine` | Open → rotate → save → reopen over the corpus: **rotation present through both readers, the prefix byte-identical ON DISK, untouched objects absent from the update**; every `malformed/` file that `requiresSaveAs()` saves to a new file that reopens with **no repair**; the unsaved delta and the saved file render identical pixels; **save time measured** on `pages-900.pdf` and a 200 MB file |
| **T9** | `RecoveryJournal` and the kill harness | T5, T7 | D3-11; §5.5; `rawpage_killprobe` | `tst_recovery` green (append, discover, replay, torn line, mid-file damage, unterminated group, undo-after-save, lock, read-only directory, null `IDurableFiles`); **the kill cases: `TerminateProcess` at random instants, twenty runs each, during commands and during saves — every acknowledged command recovered, the target never partial** |
| **T10** | The UI | T8, T9 | The five registrations; `RotatePagesDialog` (D3-13); `DocumentWindow` owns the journal; D3-14's dialog; dirty marker in the tab and title; banners for *recovery unavailable*, *save failed*, *file changed on disk*; undo and redo labels from `command.*`; `i18n-lint` families; the two inherited `tst_context` corrections | Every §8 chord this stage owns dispatches **from a key event**; `Ctrl+R` in a declared `PageThumbnails` widget dispatches the same command; **a rotated page's geometry is asserted before its pixels** (I-36's lesson); a dirty close asks; `--list-actions` re-measured |
| **T11** | `RecoveryPrompt`, `RecoveryDialog` | T9, T10 | Discovery at launch; replay; *recover to a new file*; discard; the report of step *k*; CrashHandler's sentences | `tst_recoverydialog` green; a journal written by the kill probe is offered and replayed **by the product's own composition**; the startup log no longer says *"arrives at Stage 3"* |
| **T12** | Settings keys and the Documents page | T10, T11 | D3-9's two keys | Both round-trip; `recoveryDir` redirects a new journal; `warnOnRepairedSave` off skips the explanation **and still forces Save-As** (the control) |
| **T13** | **A4 re-measured through the product**, `fuzz_cos_roundtrip`, CI, lockstep | all | D3-1's second half; D3-17; §9; §10 | **A4 PRODUCT VERDICT RECORDED** beside the probe's; three fuzz targets build, link and run; `Implementation-Plan.md`'s Stage 3 status block reflects reality, deviations included |

---

## 7. Blocking preconditions — setup checklist

| # | Precondition | Why | State |
|---|---|---|---|
| **P-1** | **Adobe Acrobat with a signature panel** — owner: **you** | A4's policy half (D3-2) and guide cases M-03 and M-06. OpenSSL proves the signed range is intact; only a validator that reads `/DocMDP` and later revisions can say whether the edit was *allowed* | ✅ **Satisfied** — Acrobat (64-bit) 26.001.21771 at `C:\Program Files\Adobe\Acrobat DC\`, installed for Stage 2's P-4. **Re-checked 2026-09-14: the `.pdf` association is still RawPage's**, so Acrobat is opened with right-click → *Open with* |
| **P-2** | **The OpenSSL command-line tool, version 3, on the `PATH` — or named by `RAWPAGE_OPENSSL` — of every job that runs A4's automated check** | D3-2. A missing tool must **fail** the case, never skip it: *A4 cannot be judged* is an S1 | ✅ **Satisfied 2026-09-14, read in CI run 34826889323** (commit `e36fff3`), whose *P-2 — the A4 signature validator* step printed, on the **Windows** job, `C:\Program Files\Git\mingw64\bin\openssl.exe`, *found by the RAWPAGE_OPENSSL environment variable*, **OpenSSL 3.5.4**; and on the **Ubuntu** `gates` job, `/usr/bin/openssl`, *found by PATH*, **OpenSSL 3.0.13**. The three `gates.validators.*` cases passed in that run's `gates`, `portable-asan` and Windows jobs. How it got there, measured the same day: **The self-hosted runner has no service account**: `recto-dev-win` runs interactively as the desktop user through `run.cmd`, so the question was not *which account* but *which OpenSSL a job's shell finds* — and the answer was one nothing in the repository controls. **Neither copy on this machine is on the registry `PATH`**: miniconda's `C:\ProgramData\miniconda3\Library\bin\openssl.exe` **3.5.6** arrives only through `conda init` in the user's PowerShell profile, and is gone under `-NoProfile` (simulated); Git for Windows' `C:\Program Files\Git\mingw64\bin\openssl.exe` **3.5.4** is on no `PATH`, and runs with only `System32` on it. **So the Windows CI job names Git's copy in `RAWPAGE_OPENSSL`**, and `tests/gates/ResolveOpenSSL.cmake` resolves the validator **at test time** — that variable when set, and then nothing else; `PATH` otherwise — and prints the path, how it was found and the version. `gates.validators.openssl` and its two controls are guide case A-06; the `gates` preset is **97/97** here. Ubuntu runners ship OpenSSL 3 on `PATH`. **What this does not establish:** that either copy verifies a CMS signature — guide case A-15's flipped-byte control does that, at T1 |
| **P-3** | **Python 3 on the same jobs** | The byte-level `/ByteRange` extractor (D3-2) and the corpus generators. Already a verification dependency (`tests/gates/RequirePython.cmake`) | ✅ Python 3.13.9 on this machine; Stage 2's `gates.corpus.*` already require it on every job |
| ~~**P-4**~~ | ⤵ **Not required — recorded because `Implementation-Plan.md` §5 queued it**: *"needs a hex editor"*. The byte comparison before and after a save is **automated** (guide §5.9), against bytes read back from disk, which is stricter than a person scrolling a hex view. The manual cases never ask for one | — | ⤵ Replaced by automation |
| **P-5** | **A way to kill the product mid-edit** — owner: **you** | Guide case M-04. The automated half kills a probe process with `TerminateProcess`; only a person can kill the real product at a moment of their choosing | ✅ Task Manager, or `Stop-Process -Name rawpage -Force` — both built in |

**No Stage 3 precondition is open** — P-2 was the last, satisfied 2026-09-14 in CI run 34826889323. Nothing
waits on the user before T1.

---

## 8. Verdict ledger — what Stage 3 owes

| Verdict | Produced by | Recorded in |
|---|---|---|
| **A4** — does an incremental save preserve the original bytes closely enough that a pre-existing signature still validates? | **T2** (probe, writer alone) and **T13** (product, through `SaveCoordinator` onto disk) — D3-1 | [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified), **both figures, the second labelled as the product's**; summarized in the Stage 3 status block |

**Stage 3 owns no open decision in `Implementation-Plan.md` §8.** O11 — how a parameterized command
reaches the action layer — is touched by `RotatePagesDialog`, which answers it the way Stage 1 did
(the surface that raised the dialog holds the parameters) and leaves it open for Stage 14, its owner.

**A stage that is green but has no verdict is not signed off.** Stage 3 owes one, measured twice.

---

## 9. CI changes

| Job | Before | After |
|---|---|---|
| `gates` (ubuntu, no Qt) | 94 cases, three gates | **Four gates.** Plus `mutation-lint`'s fixtures and control, the `pdf-cos` layer fixture and control, and the signed-corpus verification with its flipped-byte control — which needs `openssl`, present on the runner |
| `windows` (self-hosted) | 741 cases | Plus every Stage 3 suite, the kill cases, and `tst_saveengine`. **The non-empty-prefix loop is unchanged** because D3-18 adds no prefix — which is a reason for that decision, not a side effect of it |
| `portable-asan` (ubuntu) | the headless suite and the corpus parse | Plus **the writer, the sink and the round trip** under ASan+UBSan — D3-3's reason for existing |
| `portable-fuzz` (ubuntu, clang) | two targets, 60 s each per push | **Three**; the linked-targets check expects three |
| `fuzz-soak.yml` | two targets, ten minutes | Three |
| `coverage-report` | names what did not run | Gains the kill cases, which exist on Windows only, so their absence from a Linux run is named rather than inferred |

---

## 10. Lockstep debt this stage creates

**Paid in the same change as this plan** — corrections to existing documents, made now because each
is a fact this plan establishes:

- ✅ **`Implementation-Plan.md` § Stage 3**: status line; `Outline` and `Metadata` removed from the
  components and added to Stage 4's (D3-8); `PageTree`'s component entry narrowed to rotation; the
  settings keys reduced to two, with `documents/saveMode` added to Stage 8's list and
  `documents/autosaveIntervalSec` to Stage 13's (D3-9); *"Stage 3 adds seeds, not targets"* corrected
  (D3-17); the replay note refined to the undo step (D3-11). §5's guide row and its prose count.
- ✅ **`Architecture-FileMap.md`**: `Outline` → ○ (S4 read, S7 edit), `Metadata` → ○ (S4), `PageTree`'s
  edit half → ○ (S3 rotate, S7 rest), `Permissions` → ○ (S3 pre-flight, S10 permission bits); flow D's
  `ActionId::RotatePages`, which is not an id — the table's is `page.rotate` (guide issue **I-02**).
- ✅ **`Tech-Stack-Plan.md` §5.3**: `Command::name()` shown returning a translated `QString`, which
  `core/` cannot build (guide issue **I-01**).
- ✅ **`CLAUDE.md`** and **`claude-plan/README.md`**: the *next action*, and the document-set counts.

**Owed with the code**:

- ✅ Every ⚠️ row in §4, with its file — checked at T13: `WinDurableFiles`, `SaveCoordinator`, `rawpage_killprobe` and `UnsavedChangesDialog` each have their `Architecture-FileMap.md` row.
- ✅ *(paid at T8)* **Flow F** (`Architecture-FileMap.md` §5) is rewritten at T8 to §5.3's transaction: it currently
  shows `IDocumentBackend.save(Incremental)` doing the whole job, and D3-4 splits it three ways.
- ✅ *(paid at T7)* **`Tech-Stack-Plan.md` §4.3**'s interface count — seventeen — becomes eighteen at T7, and so do
  `Implementation-Plan.md` §7's §10 row, `tests/platform/`'s census assertion and `CLAUDE.md`'s
  architecture paragraph. **Counted from the header at T7**, not edited in advance.
- ✅ *(paid at T8)* **`Tech-Stack-Plan.md` §5.4**'s *"The original bytes are never rewritten"* gains D3-4's precision —
  copied, never re-serialized — at T8, where the behaviour exists.
- ✅ *(paid at T6, T7 and T9)* **`CLAUDE.md`'s gate table** flips `mutation-lint` to live at T6, and its *"Four are specified;
  THREE are live"* sentence with it. **The waiver census** in the same file is re-derived from
  `layer-lint --explain` at T7 and T9, which each add waivers — the number is not predicted here.
- ✅ *(paid at T10)* **`Implementation-Plan.md` § Stage 8's key bindings** list `Ctrl+Shift+S`, which Stage 3 now
  registers (D3-16). Annotated at T10: Stage 8 changes what the binding writes, not whether it exists.
- ✅ *(nothing to change, re-read at T13)* **`Features.md` §9.2**'s *"Autosave of in-progress edits at an interval"* is a capability statement
  and stays; D3-9 only moves the key. Recorded so a reader does not take the move for a removal.

---

## 11. Explicitly out of scope

| Not in Stage 3 | Arrives at |
|---|---|
| Every page operation except rotation — insert, delete, move, extract, replace, crop, duplicate — and the model operations behind them (D3-8) | Stage 7 |
| **Rotating a page range, odd or even pages, or pages of one orientation** — the dialog offers *current page* and *all pages* only (D3-13) | Stage 7 |
| The Thumbnails pane. `Ctrl+R` in that context is bound and resolves; there is no pane to press it in | Stage 4 |
| `Outline` and `Metadata`; Document Properties; any change to `/Info`, `/ModDate` or XMP on save (D3-5, D3-8) | Stage 4 (read), Stage 8 |
| **A full-rewrite, compacting Save-As.** Stage 3's Save-As copies and appends (D3-16) | Stage 8 |
| `documents/saveMode`, `documents/autosaveIntervalSec` (D3-9) | Stages 8, 13 |
| **Editing an encrypted document** — refused by name (D3-7) | Stage 10 |
| **Honouring a signature's modification policy before saving** — `/DocMDP`, `/FieldMDP`, and the policy Acrobat applies to an approval signature. Stage 3 saves a rotation into a signed document and Acrobat reports the signature INVALID, for approval signatures as well as the certified one (guide I-06). Those are guide case M-03's controls, not a defect | Stage 11 |
| Signing, signature validation inside the product, timestamps, revocation | Stage 11 |
| Annotations, forms, redaction | Stages 6, 9, 10 |
| *Revert to the last saved state* (`Features.md` §2.5) | Stage 8 |
| Session restore — reopening the documents that were open at quit | Stage 4 |
| Batch and CLI verbs that run a command — the path exists (`SaveCoordinator`, the registry) and no verb calls it | Stage 14 |

---

## 12. Risks inside Stage 3

| Risk | Shape | Mitigation |
|---|---|---|
| **A4 comes back no-go** | The append model cannot preserve a signature through a real reader's modification check, and every later stage that saves inherits the defect | **T2 is second**, before the model, the journal and the UI; the probe writes a delta by hand so a no-go costs the writer, not the stage |
| **The validators agree with us for the wrong reason** | A signature check that extracts the byte range through our own reader, or a *valid* from a tool that never sees an invalid file | D3-2's extractor does not use `pdf/cos/`; the flipped-byte control runs **before** the real check, and a control that passes stops the case |
| **The generated signatures are narrower than real ones** | One signer, RSA-2048, PKCS#1 v1.5, SHA-256, no timestamp, no revocation data, no PAdES profile — a real file from another producer may differ in exactly the way that matters | **Named, not mitigated away**, as D2-11's corpus limit was: the guide's §1 states the bound, and the A4 verdict is recorded *"over our generated signatures"*. The trigger to revisit is the first real signed file that does not survive |
| **The save's copy makes large files slow** | D3-4 writes every byte on every save | T8 measures it and records the figure; D3-4's *Revisit* column names the threshold question rather than pretending there is no cost |
| **Two readers, one document, and they disagree** | The COS reader and PDFium pick different pages or rotations, and a rotation lands on a page nobody saw | D3-7 refuses editing on disagreement; T4's case plants one |
| **A green round trip that compares the buffer** | I-38's shape: the replace step is never exercised by the assertion | Every byte-identity case reads the path back after the replace, and says so at the assertion |
| **The kill harness kills at convenient moments** | A random delay that always lands between commands | The probe acknowledges each command on a pipe, the harness records where each kill landed, and the case fails if twenty kills never landed inside `run()` or inside a save — **the instrument must show it reached the instants it claims** (I-41's lesson) |
| **`ui/` finds a way to mutate** | A widget reaches a non-const `Document` through a back door | D3-10 makes it a type error; `layer-lint` already denies `pdf/` to `ui/` |
| **`core/doc/` grows past D3-8's list** | A type arrives with no Stage 3 caller | D3-8 names four; a fifth in `core/doc/` during Stage 3 is a finding |

---

## 13. Definition of Done for Stage 3

[`Implementation-Plan.md` §2's Definition of Done](./Implementation-Plan.md#definition-of-done--every-stage-no-exceptions),
made concrete:

1. T1…T13 complete; the [exit criteria](./Implementation-Plan.md#stage-3--document-model-command-journal-incremental-save) met.
2. `cmake --build --preset x64-release --target checks` clean — **four** gates, with `mutation-lint`'s
   fixtures and control.
3. `ctest` green on Windows across all five targets, every Stage 0, 1 and 2 suite included.
4. `portable-asan` green including the writer and the round trip; `portable-fuzz` green on **three**
   targets; the ten-minute soak run clean on all three.
5. **Golden suite still green, and the round-trip suite green** — the first stage where both exist.
6. **The A4 bound is written into the sign-off**: the verdict holds over RawPage's generated
   signatures (D3-2), validated by OpenSSL for integrity and by Acrobat for policy, and says nothing
   about signatures from other producers.
7. **A4 has a recorded verdict, probe and product**, in `Tech-Stack-Plan.md` §3.3.
8. [`Stage3-Mutation-Testing-Guide.md`](./Stage3-Mutation-Testing-Guide.md) — written from this plan
   before the code, its automated half run green by me with **actual** output, its manual half walked
   and signed off **by you**. I do not mark a manual case.
9. No open S1 or S2 in the guide's issue log.
10. `Architecture-FileMap.md` rows flipped and §10's debt paid, in the same changes as the code.
11. `Implementation-Plan.md` Stage 3 status block updated to reality, deviations included — and the
    first milestone's banner (*"🏁 First milestone complete"*) earned rather than inherited.
