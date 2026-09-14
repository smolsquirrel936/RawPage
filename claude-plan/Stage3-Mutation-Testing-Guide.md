# Stage 3 — Document Model, Command Journal & Incremental Save · Testing Guide

> **Authoritative verification document for Stage 3.** Written from
> [`Stage3-Mutation-Plan.md`](./Stage3-Mutation-Plan.md) and
> [`Implementation-Plan.md` § Stage 3](./Implementation-Plan.md#stage-3--document-model-command-journal-incremental-save),
> **before any Stage 3 code exists** — which is the point: verification written afterwards only ever
> confirms whatever got built, including the parts built wrong.
>
> **Status:** ⚪ **Written 2026-09-14. Nothing below has been run; every box is ☐.** **83 automated**
> (A-01 … A-83) and **8 manual** (M-01 … M-08). **Two issues**, both raised by reading while this guide
> was written and both corrected in the same change — **I-01** and **I-02**, §8. No S1 or S2 is open.
>
> **Two halves, per [`CLAUDE.md`](../CLAUDE.md): [§5 Automated](#5-automated-verification--i-run-this)**
> — I run it and report the actual output — and **[§6 Manual](#6-manual-verification--you-run-this)** —
> you walk it on real hardware, and **I never mark a manual case passed on your behalf.**
>
> **The manual half is eight cases, and it is not smaller than Stage 2's by accident or larger by
> generosity.** Most of Stage 3 is bytes and bookkeeping — an update's offsets, a journal's lines, a
> model hash before and after — and every one of those is automated, including the two things
> `Implementation-Plan.md` §5 queued this guide as needing a person for: *comparing file bytes before
> and after* (automated in §5.9, against bytes read back from disk) and *killing the process mid-edit*
> (automated in §5.10 with `TerminateProcess`, at instants the harness proves it reached). What is
> left is irreducible: **a validator that judges modification policy and cannot be driven from a
> test (Acrobat), independent readers looking at the saved file, the real product killed at a moment
> you choose, and the dialogs a person meets when work is at stake.** Anything in §6 that could have
> been automated is a defect in this guide; say so and I will move it.
>
> **Label scoping.** `A-nn`, `M-nn` and `I-nn` here are **this guide's**; Stages 0, 1 and 2 have their
> own. `A4` (no hyphen) is the incremental-save **risk** in `Tech-Stack-Plan.md` §3.3. `T1`…`T13` are
> **Stage 3** tasks and `D3-n` Stage 3 decisions, both in `Stage3-Mutation-Plan.md` (§6 and §3). A
> citation of another stage's issue names the stage: *"Stage 2's I-38"*.

---

## 1. Scope

### In scope — must pass for sign-off

- **One real change, end to end**: rotate a page from the Viewer, undo it, redo it, save it
  incrementally, reopen it — through `ui/` constructing a `Command` and nothing else.
- **The original bytes survive a save.** Every byte of the file as opened is present, unchanged, at
  the start of the saved file, and an untouched object is not in the update at all.
- **A4**: a pre-signed file's signature still validates after an unrelated incremental save —
  measured twice (D3-1), judged for integrity by OpenSSL and for policy by Acrobat (D3-2).
- **Recovery holds at an arbitrary instant**: a command is recoverable once `run()` has returned;
  a save leaves the target either wholly old or wholly new.
- **The mutation gate is live**: `mutation-lint` fails on a command with no registration, no
  serialize test or no revert test, and `checks` says **four**.
- **The forced Save-As** of a repaired document, **the refusal** to edit a document this build
  cannot write, and **the question** before a document with unsaved changes is closed.
- **No regression** in any Stage 0, 1 or 2 case.

### Out of scope — do NOT file these as bugs

Planned absences, itemized in [`Stage3-Mutation-Plan.md` §11](./Stage3-Mutation-Plan.md#11-explicitly-out-of-scope).

| Not a bug this stage | Arrives at |
|---|---|
| **The Rotate Pages dialog offers only *current page* and *all pages*** — no range, no odd/even, no orientation filter (D3-13) | Stage 7 |
| No page can be inserted, deleted, moved, extracted, cropped or duplicated | Stage 7 |
| **There is no Thumbnails pane**, so `Ctrl+R` in the Page Thumbnails context cannot be pressed by hand. It is bound and it resolves; §5.11 proves that through a test widget and says so | Stage 4 |
| No bookmarks, no Document Properties, and **saving does not change `/ModDate` or XMP metadata** (D3-5, D3-8) | Stage 4, Stage 8 |
| **Save As does not compact or rewrite the file** — it copies the file and appends, exactly as Save does, to a new path (D3-16) | Stage 8 |
| No *Revert to saved* | Stage 8 |
| **An encrypted document cannot be edited at all.** Rotate is disabled and says why (D3-7) | Stage 10 |
| **RawPage saves a rotation into a certified document whose certification forbids changes, without warning.** Acrobat then reports the certification as broken. That is M-03's *control*, and the product honouring `/DocMDP` is Stage 11's | Stage 11 |
| No signing, no signature panel, no validation inside RawPage | Stage 11 |
| No session restore: the documents open at quit do not reopen at launch — only a **crash** offers recovery | Stage 4 |
| Most of the 361 declared commands are still disabled; Stage 3 registers five | per feature stage |

### What this evidence cannot tell us

**Every signed file is generated by us** (D3-2), the way every corpus file is (Stage 2's D2-11). That
keeps the repository free of anyone's key and makes the files reproducible; it also bounds what A4's
verdict means, and the bound is stated here rather than discovered at Stage 11:

> **A4 is judged over RawPage's own generated signatures**: one signer, RSA-2048, PKCS#1 v1.5, SHA-256,
> a self-signed certificate, no timestamp, no revocation data, no PAdES profile. It does **not** prove
> that a signature produced by another tool, with another profile, survives — only that the append
> model, as RawPage writes it, does not disturb a signed range and does not trip a policy check that
> should not trip.

**The two validators answer different questions, and neither answers both** (D3-2):

| Validator | Can say | Cannot say |
|---|---|---|
| **OpenSSL** `cms -verify` (automated) | The bytes named by `/ByteRange` hash to the signed digest, and the signature over that digest is good | Anything about modification policy, later revisions, `/DocMDP`, or whether the certificate is trusted — the case passes `-noverify` because the certificate is a test identity |
| **Acrobat** (manual, M-03) | Whether the signature is valid **for its revision**, whether later revisions changed what the signature permits, and — with the certified control — whether it can tell | Anything this project can automate or re-run in CI |

### Case anatomy

Every case is **ID · Goal · Pre · Steps · Expect · If it fails**, with a `☐` changed to ✅ or ❌. A case
either passes or yields a **specific** failure — "looks fine" is not a result.

### The rules this guide is written under

**Rule 1 — a case that reaches a behaviour through a back door says so at the assertion, and names
what it therefore does NOT cover** (convention 19, `Implementation-Plan.md` §1.5; Stage 1's I-34).

**Rule 2 — a repaired case is re-walked from step 1**, never signed off on the strength of the repair
(Stage 1's I-45).

**Rule 3 — a green tick is not a clean run** (Stage 2's I-38, where every fuzz run for a week logged a
sanitizer finding under a passing job). **In this stage the tick to distrust is a byte-identity case
that compares the buffer the writer produced** rather than the file on disk after the replace. Every
such case in §5 reads the path back, and says so.

**Rule 4 — an instrument is shown a known answer before its reading counts** (Stage 2's I-41). A
signature check is shown a file with one flipped byte **first**, and must say *invalid*; a kill harness
reports **where its kills landed**, and fails if none landed inside the instants it claims to cover.

**Rule 5 — a new mechanism gets a control written from what must stay untouched** (convention 22,
Stage 2's I-13). Rotating page 3 must leave every other object as it was; an undo before Save must
write nothing; a setting that suppresses a warning must not suppress what the warning is about.

### Severity

| Sev | Meaning |
|---|---|
| **S1** | **A byte of the original file changes on save.** A save reports success over a file that does not open, or that is partial. Work is lost: an acknowledged command is not recovered, a dirty document closes without asking, a recovery replays onto the wrong content. `ui/` mutates a `Document`. **A4 cannot be judged** |
| **S2** | A documented behaviour is missing or wrong in a way a later stage will build on — undo is off by one, dirty is wrong, an update carries objects it should not, a refusal is silent, a chord resolves in the wrong context, a gate passes a command it should fail |
| **S3** | A message is unclear, a figure is misreported, a stray temporary file is left behind, a diagnostic names the wrong thing |
| **S4** | Polish nobody outside the team would notice |

**An S1 here differs from Stage 2's.** There it meant the product showed something untrue about a
document. Here it means **the product changed the document in a way the user did not ask for, or lost
a change they did.** A PDF editor can ship with a rendering imperfection; it cannot ship with either
of those.

---

## 2. Build, launch, environment

`cmake`, `ninja` and `g++` are **not on this machine's `PATH`**. Every command assumes
`scripts\env.ps1` has been dot-sourced first.

```powershell
. .\scripts\env.ps1
cmake --preset x64-release
cmake --build --preset x64-release --target checks     # FOUR gates from T6
cmake --build --preset x64-release
ctest --preset x64-release --output-on-failure
ctest --preset x64-release -R roundtrip                 # the round trip (tests/roundtrip/, D3-18)
ctest --preset x64-release -R reversibility             # the property test
.\build\x64-release\bin\rawpage.exe corpus\ordinary\text-a4.pdf   # Ctrl+R now opens a dialog
```

**Validators and helpers** (P-2, P-3 in the plan's §7):

```powershell
cmake -P tests/gates/ResolveOpenSSL.cmake        # the validator the cases will use: path, how found,
                                                  #   version. RAWPAGE_OPENSSL when set, else PATH. CI's
                                                  #   Windows job sets Git's mingw64 3.5.4; PowerShell
                                                  #   here finds miniconda's 3.5.6 via the conda profile
python --version                                  # 3.13.9 measured 2026-09-14
python corpus/make-corpus.py --verify             # now includes corpus/signed/
```

**Three notes on running `rawpage.exe` from PowerShell**, carried from `CLAUDE.md` because they still
cost time: PowerShell does not wait on a GUI binary (use `Start-Process -Wait -PassThru`); an ordinary
launch runs an event loop; and `--dump-ui-strings`, `--list-actions` and `--print-screen-metrics` do not
claim the single-instance bus.

**Where things are written**, so a walker can look:

| What | Where |
|---|---|
| Recovery journals | `%LOCALAPPDATA%\RawPage\recovery\<DocumentId>-<pid>-<n>\` — `lock`, `meta.json`, `journal.ndjson` (plan §5.5), unless `documents/recoveryDir` says otherwise |
| A save in progress | `<target directory>\.<name>.rawpage-<pid>.tmp`, **deleted on success and on failure** (plan §5.3) |
| Settings | `%APPDATA%\RawPage\settings.ini` |
| Logs | `%LOCALAPPDATA%\RawPage\logs\` |

### Settings keys that affect these cases

**Defaults are owned by `core/settings/SettingsKeys.def`** and are quoted here only once T12 has
written them. Until then the column says what the plan intends; **re-read the `.def` rather than this
table** — Stage 2's I-26 was this table disagreeing with that file in four rows.

| Key | Default | Affects |
|---|---|---|
| `documents/recoveryDir` | *planned:* empty, meaning `%LOCALAPPDATA%\RawPage\recovery` | A-66, A-82, M-05 |
| `documents/warnOnRepairedSave` | *planned:* on | A-77, M-06 |
| `general/singleInstance` | on (`SettingsKeys.def`) | A-65 — two instances, one file |
| `pageDisplay/layoutMode` | `SingleContinuous` (`SettingsKeys.def`) | A-75, M-01 — a rotated page changes the positions of the pages after it |

### Test rig

| | |
|---|---|
| **Signature validator, policy half** | **Adobe Acrobat (64-bit) 26.001.21771**, precondition **P-1** (✅). Opened with right-click → *Open with*, because the `.pdf` association is RawPage's |
| **Signature validator, integrity half** | **OpenSSL 3**, resolved by `tests/gates/ResolveOpenSSL.cmake`: `RAWPAGE_OPENSSL` when set — the Windows CI job sets `C:\Program Files\Git\mingw64\bin\openssl.exe` — and `PATH` otherwise — P-2 (🟡: green here, not yet read in a CI run; the plan's §7 row says why the runner needs the variable). **A missing validator FAILS a case; it never skips one** (A-06) |
| **Independent readers** | Acrobat, **SumatraPDF 3.5.2** (MuPDF) and **Firefox 154** (pdf.js), from Stage 2's P-4. ⛔ **Edge and Chrome are disqualified** for the reason Stage 2 gave: they are PDFium, the engine that already re-reads our output in A-58 |
| **A way to kill** | Task Manager, or `Stop-Process -Name rawpage -Force` — P-5 (✅) |
| **Corpus** | `corpus/` after T1: the 61 Stage 2 files, **`corpus/signed/`**'s four, `ordinary/rotate-inherited.pdf` and `ordinary/hybrid-xrefstm.pdf`. Every file generated by a script we own, with a `PROVENANCE.md` row |
| **Monitors** | Not a Stage 3 concern. M-01 is walked on either of Stage 2's P-1 monitors |

---

## 3. Verdicts this guide must produce

A stage that is green but has no verdict is not signed off. Stage 3 owes one, measured twice.

| Verdict | Assembled from | Recorded in |
|---|---|---|
| **A4** — does an incremental save preserve the original bytes closely enough that a pre-existing signature still validates? | **Probe (T2):** A-28, with **M-03's probe walk**. **Product (T13):** A-57 and A-83, with **M-03's product walk** | [`Tech-Stack-Plan.md` §3.3](./Tech-Stack-Plan.md#33-assumptions-that-are-not-yet-verified) — **both, the second labelled as the product's**, and stated as *"over RawPage's generated signatures"* (§1) |

**A4's verdict scale**, fixed here before the measurement so it is not graded against whatever was
achieved:

| Result | Means |
|---|---|
| **go** | Over all four signed files, after a rotation and a save: **(1)** every byte of the file as opened is unchanged at the start of the saved file, read back from disk; **(2)** OpenSSL verifies every signature, and rejects each with one byte flipped in its range; **(3)** Acrobat reports every **approval** signature valid for its revision; **(4)** Acrobat **flags** the certified `/DocMDP /P 1` file — proving (3) was a judgement and not a default. The append model stands |
| **go, with named custom work** | (1) and (2) hold, and Acrobat objects to an approval signature for a named reason in the **shape** of the update — an xref-stream convention, the trailer `/ID`, a key order. The writer changes shape; the model stands |
| **no-go** | (1) or (2) fails for a reason the append model cannot avoid. **The writer's design is reopened before Stage 6 writes the first annotation** — at T2 this stops the stage |

---

## 4. Traceability — exit criterion → case

| Exit criterion ([`Implementation-Plan.md` § Stage 3](./Implementation-Plan.md#stage-3--document-model-command-journal-incremental-save)) | Closed by |
|---|---|
| Rotate → undo → redo → save → reopen works end to end | A-38, A-71, A-75 (the gesture and the page), A-51, A-58 (save and reopen through both readers), **M-01**, **M-02** |
| The original bytes are provably preserved | A-19 … A-22, A-25, A-51, A-54, A-55, A-56 |
| A pre-signed file's signature survives an unrelated incremental edit — **A4** | A-28 (probe), A-57, A-83 (product), **M-03** |
| Killing the process and relaunching offers and correctly replays the change | A-62 … A-64, A-68, A-74, **M-04** |
| `mutation-lint` is live and fails on a test command with no inverse | A-10, A-11 (the gate), A-44 (the lying inverse) — two mechanisms, as plan D3-12 says |
| *(Algorithms)* A permission refusal leaves the document bit-identical | A-36, A-40 |
| *(Algorithms)* Partial application is a bug; apply+revert restores a structural hash | A-40, A-43, A-44 |
| *(Algorithms)* Replay keeps 1…*k*−1 and reports *k* | A-63, A-67, A-82 |
| *(Algorithms)* A repaired document's first save is forced to Save-As | A-25, A-52, A-77, **M-06** |
| *(Testing)* A read-only recovery directory degrades to memory with one warning | A-66, A-80, **M-05** |
| *(Plan D3-14)* A dirty document is never closed without a disposition | A-78, **M-07** |
| *(Plan D3-4)* A save is never partial on disk | A-48, A-54, A-69, **M-08** |

---

## 5. Automated verification — I run this

Every case names its command and its pass condition. I report the **actual** output; a failing case is
reported as failing, with that output, and is never declared fixed without a re-run. **When a case is
ticked it carries the evidence it was ticked on** — Stage 1's I-32 was a guide whose summary claimed a
green its cases did not record.

### 5.1 Build, boundary, environment

**A-01 · Configure and build, both presets**
☐ **Goal** Stage 3's sources compile under the pin, with `-Werror` in `core/`, `pdf/cos/` **and `pdf/ops/`**.
**Steps** `cmake --preset x64-debug`, `--preset x64-release`; build both.
**Expect** Exit 0 on all four; no warning in `core/`, `pdf/`, `platform/`. **If it fails** S1.

**A-02 · The `ui/` warning grace list is still empty**
☐ **Steps** Read `RAWPAGE_UI_WARNING_GRACE` in `cmake/RawPageWarnings.cmake`.
**Expect** Empty. Three new dialogs are new code with no legacy. **If it fails** S3.

**A-03 · `rawpage_ops` joins the headless suite without bringing an engine — D3-3**
☐ **Goal** The portable half grows and `rawpage_assert_headless()` still means what it says.
**Steps** `ctest -R "gates.headless_assert"` — with a new case, **`ops_edge_is_allowed`**, beside Stage 2's five, and **`ops_carrying_pdfium_is_caught`**, where a fixture `rawpage_ops` links `rawpage_pdfium`.
**Expect** The control (`clean_closure_is_allowed`) and `ops_edge_is_allowed` pass; `ops_carrying_pdfium_is_caught` **fails configure with the assertion's message**. Runs on the bare `gates` preset.
**If it fails** S1 — the writer would be linked into a suite that claims no engine, or kept out of the one that has ASan.

**A-04 · `portable-asan` configures, builds `rawpage_ops`, and runs the Stage 3 unit suites**
☐ **Goal** D3-3's reason for existing: the writer and sink under ASan+UBSan.
**Steps** The `portable-asan` CI job. **Read the log, not the tick** (rule 3).
**Expect** Configure succeeds; `rawpage_ops` builds; the log names `tst_incrementalwriter`, `tst_mutationsink` and the round-trip cases as run; **no `runtime error:` and no `ERROR: AddressSanitizer` line anywhere**.
**If it fails** S1 for a configure or a sanitizer finding; S2 if the suites are absent from the log.

**A-05 · `pdf/cos/` cannot include `pdf/ops/` or `pdf/render/` — D3-3**
☐ **Steps** `ctest -R "gates.layer_lint.pdf_cos"` over three fixtures: `pdf/cos/` including `pdf/ops/`; `pdf/cos/` including `pdf/render/`; and the **control**, `pdf/ops/` including `pdf/cos/`.
**Expect** The first two fail naming the `pdf-cos` layer; the control passes. `layer-lint --explain` lists the entry. **If it fails** S2.

**A-06 · The validators are present, and their absence is a failure — P-2, P-3**
☐ **Goal** *A4 cannot be judged* must never read as *A4 passed*.
**Steps** `ctest -R "gates\.validators\."` — three cases over `tests/gates/ResolveOpenSSL.cmake`, which resolves OpenSSL **at test time** (`RAWPAGE_OPENSSL` when set, and then only it; `PATH` otherwise): `gates.validators.openssl`; the control `…openssl_absent_fails`, run with a `PATH` holding no OpenSSL and no override; and the control `…openssl_override_not_a_file_fails`. Python's half is `gates.corpus.*`, which already fails through `RequirePython.cmake` when there is no interpreter. In CI, read the *P-2 — the A4 signature validator* step of both jobs.
**Expect** The first case passes and its output names the path, how it was found, and a version line starting `OpenSSL 3.`; **both controls FAIL the script** — exit 1, naming the missing tool or the bad variable, never `SKIPPED` and never a fall-back to `PATH`. The Windows job's step names `C:\Program Files\Git\mingw64\bin\openssl.exe`. **What the three do not cover:** that the tool verifies a CMS signature — that is A-15's flipped-byte control and its real check, which runs OpenSSL through the same resolver. **If it fails** S1.

**A-07 · The product still starts, and names no cryptography**
☐ **Steps** `$env:QT_QPA_PLATFORM="offscreen"; rawpage.exe --dump-ui-strings`; `objdump -p .\build\x64-release\bin\rawpage.exe | Select-String "DLL Name"`.
**Expect** Exit 0, every `command.*` and `recovery.*` key resolved in both locales. Imports still `msvcrt.dll`, never `ucrtbase.dll`; **no `libcrypto` or `libssl`** — which is D3-2's independence claim measured rather than assumed. **If it fails** S2; S1 if a crypto DLL appears, because the automated A4 validator would no longer be independent.

### 5.2 The gates — four

**A-08 · `checks` clean, and it says four**
☐ **Steps** `cmake --build --preset x64-release --target checks`
**Expect** Exit 0; **four** gates named and run; `cmake/RawPageChecks.cmake` no longer says *"THREE of the four"*. **If it fails** S2 — and a `checks` claiming four while running three is the failure this gate set exists to prevent.

**A-09 · The layer and filesystem-write census**
☐ **Steps** `layer-lint.exe --root . --explain`.
**Expect** `ReplaceFileW(` and `MoveFileExW(` are in the `filesystem-write` patterns, with a fixture each and a **control** (the same call in a comment, which must not fire). **Every waiver is named**, and the new ones are in `platform/win/WinDurableFiles.cpp` and `core/cmd/RecoveryJournal.cpp` only. **The count is recorded from the output**, not predicted here. **If it fails** S2 for an unnamed waiver; S3 for a count that disagrees with `CLAUDE.md`, which is then corrected.

**A-10 · `mutation-lint` fails on each of its three rules**
☐ **Goal** D3-12. Each fixture must fail **for its own rule**, not for being malformed (convention 7).
**Steps** `ctest -R "gates.mutation_lint"` over `tests/gates/fixtures/mutation-lint/`: a command class with **no `Commands.def` row**; a registered command with **no `[serialize:Class]` test**; a registered command with **no `[revert:Class]` test**.
**Expect** Each fails and its output names **that** rule and **that** class. **If it fails** S2.

**A-11 · `mutation-lint` passes a complete command, and the real tree**
☐ **Steps** The control fixture — row, both tags — and `mutation-lint` over the repository.
**Expect** Both pass. The real run reports **one** command, `RotatePageCommand`. **If it fails** S2 — a gate that fails everything proves nothing about what it fails.

**A-12 · `i18n-lint` knows the two new families**
☐ **Steps** `i18n-lint.exe --root . --explain`; a fixture referencing `command.page.noSuchKey`.
**Expect** `keyFamilies` lists `command` and `recovery`; the fixture fails `unknown-key`; every key Stage 3 references exists in `en` and `zh-TW`. **If it fails** S2 — Stage 2's I-04 was eighteen keys referenced and defined nowhere.

**A-13 · `license-audit` is unchanged**
☐ **Steps** The `checks` invocation.
**Expect** Green, and the component list unchanged — Stage 3 links nothing new. **If it fails** S2.

### 5.3 The writer's corpus

**A-14 · The corpus regenerates byte-identically, signed files included**
☐ **Steps** `python corpus/make-corpus.py --verify`, `--check-provenance`, `--self-test`.
**Expect** All three exit 0; `signed/` and the two new `ordinary/` files are in the regenerated set, each with a `PROVENANCE.md` row saying what it is for. *"Signing is Stage 10"* is corrected to 11 in `PROVENANCE.md` and `make-encrypted.py`. **If it fails** S2 — a signature is bytes, and D3-2's determinism is what lets a signed file be regenerated at all.

**A-15 · Every generated signature verifies — after its control has failed**
☐ **Goal** Rule 4 applied to the validator before anything depends on it.
**Steps** `ctest -R "gates.corpus.signed"`: **first**, for each of the four files, flip one byte inside the signed range and run the extractor and `openssl cms -verify -binary -inform DER -noverify`; **then** run the same on the unmodified files.
**Expect** Every flipped copy is **rejected**; every original **verifies**; the output names the OpenSSL path and version. A control that verifies **stops the case** rather than letting the real check run.
**If it fails** S1 — every later A4 case reads this instrument.

**A-16 · The signed files have the shapes the plan says**
☐ **Steps** The byte-level script — **not** `pdf/cos/` — reads each file.
**Expect** `approval.pdf`: a classic table, one approval signature. `approval-xrefstream.pdf`: an xref stream. `certified-nochanges.pdf`: a `/Perms /DocMDP` entry whose transform parameters carry `/P 1`. `approval-then-update.pdf`: **two** revisions, the signature's range ending at the **first** `%%EOF`. **No committed file contains a private key** (no `PRIVATE KEY` block, no PKCS#8 structure), and each certificate's subject names itself a corpus test identity.
**If it fails** S2; S1 if a key is committed.

**A-17 · The new ordinary files say what they claim — convention 22**
☐ **Steps** `tst_pdfiumdoc` over `rotate-inherited.pdf`; `tst_xreftable` over `hybrid-xrefstm.pdf`.
**Expect** `rotate-inherited.pdf`: the engine reports `/Pages`-inherited 90°, an explicit 0° override, and a second inheriting kid at 90°. `hybrid-xrefstm.pdf`: at least one object reachable **only** through `/XRefStm`, which `tst_xreftable`'s hand-built buffer then stops being the sole witness for. **If it fails** S2 — a fixture that does not carry its fault tests nothing.

**A-18 · Golden manifest covers the new files, and nothing else moved**
☐ **Steps** `golden-update.exe --check-manifest`; `git diff --stat golden/`.
**Expect** Green. Each new file is a case or an excluded row with a reason. **The 198 existing baselines are byte-unchanged**, and `golden/UPDATES.md` carries one T1 entry for the additions only. **If it fails** S2 — a baseline regenerated alongside new files is how a regression is laundered (D2-3).

### 5.4 `ObjectAllocator`, `IncrementalWriter`, and the A4 probe

**A-19 · An update after a classic table**
☐ **Steps** `tst_incrementalwriter` — a hand-built `/Rotate` change on `text-a4.pdf`.
**Expect** The new bytes re-read through `XrefTable` with the rotation present; **every byte before the old end of file is identical**; the section lists **exactly one** object; the trailer's `/Prev` is the old `startxref`; `/Size` is not reduced. **If it fails** S1.

**A-20 · An update after an xref stream, and after a hybrid file**
☐ **Steps** The same over `objstm-xrefstream.pdf`, `approval-xrefstream.pdf` and `hybrid-xrefstm.pdf`.
**Expect** After a stream: an xref **stream**, Flate-encoded, with an object number from `ObjectAllocator` at or above the old `/Size`. After the hybrid file: a classic section, and the object reachable only through `/XRefStm` (A-17) **still resolves** after the update — the control for losing packed objects. **A page dictionary that lived inside an `/ObjStm`** is written as an ordinary object under its own number, the new section's type-1 entry supersedes the old type-2 one, and **the object stream itself is not in the update** (D3-5). **If it fails** S1.

**A-21 · A fourth generation on a three-generation file**
☐ **Steps** Over `incremental-3gen.pdf`.
**Expect** Four sections on the `/Prev` chain; **newest wins** for every object Stage 2's A-20 checks, plus the new one; generations 1–3 byte-identical. **If it fails** S1.

**A-22 · Untouched means untouched — convention 22**
☐ **Steps** For every ordinary corpus file: record every object's raw bytes by newest-wins resolution; write a one-page rotation; resolve again.
**Expect** Exactly one object differs. `/Info`, every stream, the `/Pages` nodes and every other page are **the same bytes at the same offsets**. **If it fails** S1.

**A-23 · The allocator never reuses a freed number**
☐ **Steps** `tst_objectallocator` over a file with free-list entries below `/Size`.
**Expect** Every allocation is at or above `/Size`; no free entry is reused (plan D3-5). **If it fails** S2.

**A-24 · The writer is deterministic, and changes `/ID`'s second half only**
☐ **Steps** Write the same delta twice over the same bytes.
**Expect** Identical output. `/ID[0]` is unchanged; `/ID[1]` differs from the original and is the same in both runs. **If it fails** S2 — reproducible output is what lets an expected update be a fixture.

**A-25 · A repaired file gets a complete table and no `/Prev` — D3-6**
☐ **Steps** For every `corpus/malformed/` file whose `RepairReport::requiresSaveAs()` is true: write the repair-variant update over its bytes, then re-read with `XrefRepair::openWithRepair()`.
**Expect** No `/Prev` in the new trailer; the re-read's `RepairReport` has **no entry above `Info`**; every object the original repair recovered resolves; the original bytes are unchanged. **The list of files is printed**, so a case that iterated nothing is visible. **If it fails** S1 for changed bytes; S2 for a file that still needs repair.

**A-26 · The writer never reads outside its buffer**
☐ **Steps** A-19 … A-25 under `portable-asan` (the CI job).
**Expect** No sanitizer line in the log. **If it fails** S1.

**A-27 · The writer refuses what it cannot write**
☐ **Steps** An encrypted corpus file; a delta naming an object number that does not exist; a delta whose object has no serialization (a constructed stream with no data).
**Expect** Each refused with a named `Result` error and **no bytes written**. **If it fails** S2.

**A-28 · A4 PROBE — the writer alone, over the signed corpus (D3-1, first half)**
☐ **Goal** Early enough that a no-go costs the writer, not the stage.
**Steps** For each of the four signed files: a hand-built rotation of page 1, written by `IncrementalWriter`, to a file. Then A-15's procedure over the **written file** — control first.
**Expect** Every flipped copy rejected; every signature verifies; the signed revision — the file up to the end of `/ByteRange` — is byte-identical to the original. **Then M-03's probe walk over the same four outputs.** Verdict per §3, recorded in `Tech-Stack-Plan.md` §3.3 as the probe's.
**If it fails** **S1 and a no-go**: the stage stops at T2.

### 5.5 `pdf/ops/` — `PageOps` and `MutationSinkImpl`

**A-29 · The page-object walk agrees with the engine**
☐ **Steps** `tst_pageops` over every corpus file the engine opens, compared with `tst_pdfiumdoc`'s page counts.
**Expect** The same count and order for every file both open. `pages-loop.pdf` terminates; `pages-count-wrong.pdf` gives **3**, not the 97 its `/Count` claims. **If it fails** S1 — a rotation would land on a page nobody saw.

**A-30 · Inherited `/Rotate` is written on the page, and only there — D3-5**
☐ **Steps** Over `rotate-inherited.pdf`: rotate the inheriting kid by 90°; inspect the delta. Then rotate it by 270° more.
**Expect** First: the delta holds **only** that kid, with `/Rotate 180`; the `/Pages` node and the sibling are not in it. Second: desired state equals the inherited value and the kid had no `/Rotate` on disk, so **the delta is empty**. **If it fails** S1 — writing `/Rotate` on the parent rotates the siblings.

**A-31 · Undo before save writes nothing — the control**
☐ **Steps** Rotate page 1, revert; rotate page 1 four times by 90°.
**Expect** An **empty** delta both times, and a save afterwards reports *nothing to save* rather than writing an empty update. **If it fails** S2.

**A-32 · Edits are computed against the newest revision on disk**
☐ **Steps** Rotate, save (the adopt step), revert, inspect the delta.
**Expect** The delta holds the page dictionary with its **pre-rotation** value, because the newest revision is now rotated. **If it fails** S1 — the undo would be lost on the next save.

**A-33 · Rotation values are normalized, and an impossible one refused without trace**
☐ **Steps** `setPageRotation` with −90, 450, 45, and a page index past the end.
**Expect** −90 → 270 and 450 → 90; 45 and the bad index refused with `InvalidArgument` and **no change** to the sink's state. **If it fails** S2.

### 5.6 `Document`, `DocumentId`, `Permissions`

**A-34 · Hydration copies the engine's pages into the model**
☐ **Steps** `tst_document` over a COS-backed test backend (unit) and the real backend (`tst_saveengine`).
**Expect** `Document::pages()` equals the backend's tree for every corpus file; after a rotation the **model** differs and the backend's does not (D3-15). **The unit half says at its assertion that it does not cover the engine's tree.** **If it fails** S2.

**A-35 · `DocumentId` notices a changed file**
☐ **Steps** Same file twice; a touched modification time; a same-size, same-time content change; two spellings of one Windows path.
**Expect** Equal; different; **different** (the hash); equal — normalized by hand per convention 11, identically on `portable-asan`. **If it fails** S2; S1 for the content change, which is D3-16's whole guard.

**A-36 · Editing is refused by name where this build cannot write — D3-7**
☐ **Steps** `tst_permissions` over all five `encrypted/` files, a planted engine/COS page-count disagreement, a planted rotation disagreement, and every `ordinary/` file.
**Expect** Each encrypted file refused with its encryption key; each disagreement with its own key; every ordinary file editable. `ErrorCode::PermissionDenied` for all refusals, and **three distinct catalog keys**. **If it fails** S2; S1 if an encrypted file is editable.

**A-37 · The backend implements three of seven**
☐ **Steps** `tst_backend` census; the startup log.
**Expect** `IDocumentBackend`, `IPageRasterizer`, `MutationSink`; `mutationSink()` null for a closed or unknown handle; handles never reused. **If it fails** S2.

### 5.7 The command journal, the registry, reversibility

**A-38 · Run, undo, redo, and the stacks**
☐ **Steps** `tst_journal`.
**Expect** Undo reverts the newest; redo re-applies it; a `run` after an undo **empties redo**; undo with nothing to undo returns false and changes nothing; subscribers get `mutated(Scope::page(p))` once per operation; an unsubscribed token gets nothing. **If it fails** S2.

**A-39 · Dirty is a position, not a revision — D3-10**
☐ **Steps** rotate → save → rotate → undo; rotate → save → undo → rotate another page; and every revision along the way.
**Expect** First: **not dirty**, while the revision has **risen** at every step (the control). Second: dirty, and **no sequence of undos reaches clean** — the saved position is unreachable. **If it fails** S2 — a user asked to save an unchanged file learns to ignore the question.

**A-40 · A refused or failed command leaves no trace**
☐ **Steps** A planted command whose `apply()` returns false; an encrypted-document fixture refused by `Permissions`.
**Expect** Model hash, sink delta, journal lines, revision, position and both stacks **unchanged**; no subscriber called. In a debug build, the partial-application assertion fires for a planted command that changes the model **and** returns false — observed through a test assertion handler, and the case says it does not observe a real `abort`. **If it fails** S1.

**A-41 · Groups are one undo step**
☐ **Steps** A group of twelve rotations; a nested group; an empty group.
**Expect** One undo reverts all twelve; the nested group folds into the outer; the empty group records **no** step. **If it fails** S2.

**A-42 · The registry deserializes what it registered, and refuses what it did not**
☐ **Steps** `tst_command_serialize`: every `Commands.def` row; an unknown type id; each command's `name()`.
**Expect** Every row round-trips; the unknown id returns `NotFound` without a crash; `name()` is a **catalog key** present in both locales (D3-10, I-01). **If it fails** S2.

**A-43 · Reversibility over the corpus — the property test**
☐ **Steps** `ctest -R reversibility`: every registered command × every editable corpus file × the first and last page × 90°, 180°, 270°. Tagged `[revert:RotatePageCommand]`.
**Expect** Apply then revert restores the **model hash** and leaves an **empty sink delta**. The case prints how many combinations ran. **If it fails** S1.

**A-44 · A lying inverse is caught — the control for A-43**
☐ **Steps** Register a fixture command whose `revert()` returns true and restores nothing; run A-43's harness over it.
**Expect** The harness **fails** and names the command. Run **before** A-43 is trusted. **If it fails** S2 — A-43 is then an instrument that cannot read a wrong answer.

**A-45 · Hostile serialized commands**
☐ **Steps** Deserialize missing fields, wrong types, a page index of −1 and of 10⁹, an angle of 45, and a 10 MB string; under `portable-asan` too.
**Expect** Each refused by `Result`; no crash; no sanitizer line. **If it fails** S1 — the recovery journal is a file on disk and this is its parser.

### 5.8 Durable files — `platform::IDurableFiles`

**A-46 · Eighteen interfaces**
☐ **Steps** `ctest -R "platform.census"`; `rawpage.exe --version` startup log.
**Expect** The census asserts **eighteen** against the header, and `IDurableFiles` is non-null on Windows. `Tech-Stack-Plan.md` §4.3 has the row. **If it fails** S2.

**A-47 · A replace keeps the target's streams and creation time — and the check can see a loss**
☐ **Steps** `tst_durablefiles`: plant a `:Zone.Identifier` stream and a known creation time on a target; `replaceFile(target, replacement)`. **Control:** replace a second, identically prepared target with a plain `MoveFileExW`.
**Expect** After `replaceFile`: the stream is present with its bytes, the creation time kept, the content the replacement's. After the control: the stream is **gone** — proving the read-back can observe exactly the loss it guards against. **If it fails** S2; the control passing (stream present) makes the case void.

**A-48 · A replace that cannot happen leaves both files**
☐ **Steps** Target held open by another handle with no delete sharing; target read-only; replacement missing.
**Expect** Each fails with a named key; the target and the replacement are both intact. **If it fails** S1.

**A-49 · `syncToDisk` is called, and what that proves is stated**
☐ **Steps** Flush a written file; flush a path that does not exist.
**Expect** Success; failure by key. **The case says at its assertion that no test can observe durability across a power loss** — it proves the call is made and checked. **If it fails** S2.

### 5.9 The save path and the round trip

**A-50 · `SaveCoordinator` decides — every branch, with fakes**
☐ **Steps** `tst_savecoordinator` with a fake backend and a fake `IDurableFiles`: clean document; `requiresSaveAs()`; `DocumentId` mismatch; then a **failure injected at each of the six steps** of plan §5.3.
**Expect** Clean → `Unavailable`, nothing written. Repaired and mismatched → the Save-As branch, never an in-place write. Each injected failure → the target untouched, the temporary file deleted, the journal still dirty. **The case says it does not exercise the real replace** (A-47, A-57 and A-69 do). **If it fails** S1.

**A-51 · THE ROUND TRIP — open, rotate, save, reopen, over the corpus**
☐ **Steps** `ctest -R roundtrip` (`tests/roundtrip/`, unit): for every editable corpus file, rotate one page through `CommandJournal`, save through `SaveCoordinator` to a temporary directory, **read the saved file back from its path**, reopen through `pdf/cos/`.
**Expect** The rotation present; **the file as opened is a byte-identical prefix of the file on disk**; the update holds only the changed page; reopening adds **no** `RepairReport` entry. The assertion says it read **the path**, not the buffer (rule 3). **If it fails** S1.

**A-52 · A repaired document's save is a new file that needs no repair — D3-6**
☐ **Steps** For each `requiresSaveAs()` malformed file: rotate, save (which becomes Save-As to a new path), reopen the new file through `pdf/cos/` and through PDFium.
**Expect** Both open it **with no repair**; the original file's hash is unchanged. **If it fails** S1 for a changed original; S2 for a new file that still needs repair.

**A-53 · A file changed on disk is never appended to — D3-16**
☐ **Steps** Open, rotate, overwrite the file on disk with other bytes, save.
**Expect** The Save-As branch; the overwritten file unchanged; no temporary file left. **If it fails** S1.

**A-54 · The re-read guards the replace — the control for step 3**
☐ **Steps** Inject a writer fault that produces an update its own reader cannot parse; save through the **real** `IDurableFiles`.
**Expect** `replaceFile` is **never called**; the target's hash is unchanged; the save fails by key; no temporary file remains. **If it fails** S1.

**A-55 · Save after save, and undo after save**
☐ **Steps** Rotate, save; rotate another page, save; undo, save; reopen.
**Expect** Three updates on the `/Prev` chain; the final file has the first rotation and **not** the second; every earlier revision's bytes unchanged. **If it fails** S1.

**A-56 · Save As moves the document, and leaves the original alone**
☐ **Steps** Rotate, Save As to a new path; rotate again, Save.
**Expect** The second Save writes to the **new** path; the original file's hash is unchanged since open; the recovery `meta.json` names the new path. **If it fails** S1 for a changed original; S2 otherwise.

**A-57 · A4 PRODUCT — automated half (D3-1, second half)**
☐ **Steps** `tst_saveengine`: each signed file opened through the **real backend**, rotated through `CommandJournal`, saved through `SaveCoordinator` with the **real** `WinDurableFiles`; then A-15's procedure, control first, over **the file on disk**.
**Expect** Every flipped copy rejected; every signature verifies; the signed revision byte-identical. **If it fails** S1.

**A-58 · The engine re-reads what we wrote, and draws the same page — D3-15**
☐ **Steps** `tst_saveengine`: PDFium opens every file A-51 saved. Then render the rotated page twice — from the **saved file**, and from the **original** with the unsaved rotation delta.
**Expect** Same page count; the engine's rotation equals the model's; **the two renders are pixel-identical**. **If it fails** S1 for a file the engine cannot open; S2 for different pixels — a page rotated twice is the trap `PdfiumRasterizer.cpp` warns about.

**A-59 · An in-flight tile from before a rotation is never shown**
☐ **Steps** A gated fake rasterizer holds a tile of page *p* at revision *r*; rotate *p*; release the gate.
**Expect** The delivery at *r* is **dropped**; page *p*'s next tile is at *r*+1; other pages' cached tiles are **re-keyed, not re-rendered** (cache statistics). **If it fails** S1 — a stale tile after a mutation is the bug D2-12 was written against.

**A-60 · Stray temporary files are cleaned**
☐ **Steps** Leave a `.<name>.rawpage-<pid>.tmp` from a dead process id beside a document; save.
**Expect** The stale file is removed; a temporary file belonging to a **live** process id is not. **If it fails** S3.

**A-61 · Save cost, measured and reported**
☐ **Steps** `rawpage_bench_save` (reports, never fails on speed): save after one rotation on `pages-900.pdf` and on a generated 200 MB file; also `DocumentId`'s hash at open.
**Expect** Figures recorded in the Stage 3 status block with the machine; exit code about whether the measurement was **possible**. **If it fails** S3 — and a slow figure is D3-4's *Revisit*, not a failure.

### 5.10 Recovery

**A-62 · Append, discover, replay, truncate**
☐ **Steps** `tst_recovery`: run three commands; discover from a second journal object; replay into a fresh document; save; discard.
**Expect** The `{"format":1}` line first; replay reproduces the model hash **and the undo history**; after save the lines are gone and `meta.json` names the saved revision, replaced atomically; discard removes the directory. **If it fails** S1.

**A-63 · Damage stops replay where it is, and only there**
☐ **Steps** A trailing line with no newline; a malformed line at step 2 of 5; an unterminated trailing group; a failure inside the second of three groups.
**Expect** Torn trailing line: ignored **and reported**, all complete steps replayed. Malformed step 2: step 1 kept, step 2 named. Unterminated group: **dropped and reported** (D3-11). Failure in group 2: group 1 kept, group 2 wholly absent, the report names it. **If it fails** S1 for replaying past damage; S2 for a silent drop.

**A-64 · An undo after a save is recovered**
☐ **Steps** Rotate, save, undo, kill the journal's owner (drop it without closing), replay.
**Expect** The replayed document is **un**-rotated relative to the saved file, because the undo line carried the command and replay applied its `revert()`. **If it fails** S1.

**A-65 · A live journal is never offered**
☐ **Steps** Hold a journal open in one object; discover from another; then kill the owning test process (a child) and discover again.
**Expect** Not offered while its lock is held; offered once the lock is stale. Two instances with `general/singleInstance` off each get their **own** directory. **If it fails** S1 — offering a live journal replays someone's open document into another window.

**A-66 · An unwritable recovery directory degrades to memory, once**
☐ **Steps** Point the journal at a directory with write denied; run ten commands. Separately, construct it with a null `IDurableFiles`.
**Expect** Editing works; **exactly one** warning over ten commands; nothing on disk. With null `IDurableFiles`: the same degradation, and the log says why. **If it fails** S2; S1 if editing is refused.

**A-67 · Recover to a new file when the source has changed**
☐ **Steps** Journal three rotations; replace the source with a two-page file; replay.
**Expect** Offered as *recover to a new file*; replayed onto the current source into an **untitled** document; a rotation naming a missing page stops at that step and is named. **If it fails** S1 for a replay onto the changed source under its own path.

**A-68 · KILL during commands — twenty times (D3-11)**
☐ **Goal** A command is recoverable once `run()` has returned — at every instant, including inside `run()`.
**Steps** `ctest -R "platform.recovery.kill_commands"`: `rawpage_killprobe` runs rotations in a loop, writing `ack k` to a pipe after each `run()` returns; the harness calls `TerminateProcess` after a random delay, then discovers and replays. Twenty runs. **The probe also reports the phase it was in** — inside `apply`, between apply and the flush, inside the flush, between commands.
**Expect** Every run: replayed steps ≥ the last acknowledged *k*, and ≤ *k*+1; no replay error except a reported torn line. **Across the twenty: at least one kill landed inside the flush and at least one between apply and the flush** — otherwise the case **fails as unmeasured** (rule 4). No crash handler runs; the case says this is the harsher condition. **If it fails** S1.

**A-69 · KILL during saves — twenty times (D3-4)**
☐ **Steps** `ctest -R "platform.recovery.kill_saves"`: the probe saves in a loop over a copy of `approval.pdf` carrying a planted `Zone.Identifier` stream; killed at random; phases reported as in A-68.
**Expect** Every run: the target's hash is **either** the previous revision's **or** the new one's — never anything else; the stream is present; a stray temporary file is cleaned by the next save (A-60). **At least one kill landed between writing the temporary file and the replace.** **If it fails** S1.

**A-70 · Hostile journals**
☐ **Steps** A generated set of broken `journal.ndjson` and `meta.json` files — binary junk, a line of 10 MB, deep JSON nesting, a `meta.json` naming a missing source — through discovery and replay, under `portable-asan` too.
**Expect** Each reported, none crashes, no sanitizer line. **If it fails** S1.

### 5.11 The UI — `rawpage_ui_tests`, offscreen

**A-71 · Five registrations, dispatched from key events**
☐ **Steps** `tst_mutationcommands`: send `Ctrl+R`, `Ctrl+Z`, `Ctrl+Y`, `Ctrl+Shift+Z`, `Ctrl+S` and `Ctrl+Shift+S` to a shown `MainWindow` with a document, the viewer focused. Then `rawpage.exe --list-actions`.
**Expect** Each dispatches its command **from the key event**. `--list-actions` shows `file.save`, `file.saveAs`, `edit.undo`, `edit.redo` and `page.rotate` registered, and the header's three counts **are recorded from the output**. **If it fails** S2.

**A-72 · `Ctrl+R` in the Page Thumbnails context — through a back door, named**
☐ **Steps** A test widget declares `KeyContext::PageThumbnails`, takes focus, receives `Ctrl+R`. **Negative:** `Ctrl+R` with focus in the tool finder's search box.
**Expect** The widget's press dispatches `page.rotate` in `PageThumbnails`; the search box's does **not** dispatch. **The assertion says no `ThumbnailsPane` exists and no pane gesture is covered** (rule 1; D3-13). **If it fails** S2.

**A-73 · The Rotate Pages dialog constructs commands and nothing else**
☐ **Steps** Drive the dialog: *current page*, 90° clockwise; *all pages*, 180°; Cancel. A recording journal receives what it constructs.
**Expect** One command, ungrouped; one group of *n* commands, **one** undo step; nothing. The dialog holds no `Document` reference at all. **If it fails** S2.

**A-74 · The product's own composition replays a journal**
☐ **Steps** Launch `rawpage.exe` offscreen with a recovery directory holding a journal written by `rawpage_killprobe` (A-68), through the parameterized twin of the recovery prompt.
**Expect** The document opens with the replayed rotations, dirty, and its undo history intact. **The case says it uses the twin rather than the dialog** (convention 16). **If it fails** S1.

**A-75 · Geometry first, then pixels — Stage 1's I-36**
☐ **Steps** A portrait page in `SingleContinuous`; rotate it 90°; `activate()` every layout; read the page's rectangle from the layout; `grab()`.
**Expect** **First** the rectangle is landscape and the next page's top has moved by the difference; **then** the grabbed page region differs from before and matches the rotated fake tile. Undo restores both. **If it fails** S2.

**A-76 · Dirty is visible, and so is why a command is off**
☐ **Steps** Rotate; save; undo to saved; hover state of Save and Undo with nothing to do; switch to `zh-TW`.
**Expect** The tab and window titles mark a dirty document and unmark it after save **and** after undoing to the saved position. Save and Undo are disabled with their `unavailableKey` text. Undo's label is the command's `command.*` entry, in both locales. **If it fails** S3; S2 for a wrong dirty marker.

**A-77 · A repaired document's Save goes to Save-As — and the setting cannot stop it**
☐ **Steps** Open a `requiresSaveAs()` file through `FakeBackend`; rotate; `Ctrl+S`. Repeat with `documents/warnOnRepairedSave` off.
**Expect** First: the explanation, then the Save-As twin. Second — **the control** — no explanation, and **still** Save-As; no in-place save is attempted. **If it fails** S1 for an in-place save.

**A-78 · Closing a dirty document asks — every way it can close (D3-14)**
☐ **Steps** Dirty document; close the tab; close the window; tear the tab out and close that window; quit with two dirty documents; each with Cancel, Discard and Save. Then a headless close with no disposition.
**Expect** Every route asks once per dirty document. Cancel: open and dirty. Discard: closed and **its recovery directory gone**. Save: saved, then closed. Headless: refused with `Unavailable`, document still open. **If it fails** S1.

**A-79 · A document this build cannot edit says so**
☐ **Steps** Open an encrypted document through `FakeBackend` configured as the real one refuses (A-36).
**Expect** Rotate is disabled with the encryption `unavailableKey`, and `Ctrl+R` does not dispatch. **If it fails** S2.

**A-80 · The three new banners**
☐ **Steps** Degrade the journal (A-66's first half); fail a save; save over a file changed on disk.
**Expect** One banner each, of its own kind; the recovery banner appears **once**, not once per command. **If it fails** S3.

**A-81 · Stage 2's cases that Stage 3 overturns are moved, not deleted**
☐ **Steps** Read `tests/ui/tst_context.cpp` and the Viewer-subset case after T10.
**Expect** *"A chord whose command is not registered yet leaves the key alone"* uses a chord still unregistered after Stage 3, and still asserts the key comes back unaccepted; the neighbouring comment no longer predicts `edit.selectAll`. The Viewer-subset list no longer contains `Ctrl+R`, **is derived from `KeyBindingDefaults.def` against the registry** rather than typed, and passes. **If it fails** S3 — and a case deleted rather than moved is S2, because what it proved is gone.

### 5.12 Settings, recovery prompt, fuzzing, CI, and everything before

**A-82 · The two keys, the Documents page, the prompt, and the crash handler's sentences**
☐ **Steps** Round-trip both keys; set `documents/recoveryDir` and run a command; open Preferences; drive `RecoveryDialog` over a discovered journal (offscreen); read the startup log.
**Expect** Both keys round-trip; a new journal lands under the configured directory; the Documents page shows both keys beside whatever else `SettingsKeys.def` declares in that group (**six** pages of 26, counted from `PreferencesDialog::shippedPages()`); the dialog lists the journal, replays it, and on a failure names step *k*; the log no longer says *"RecoveryJournal arrives at Stage 3"*, and `app/CrashHandler.h` states D3-11's decision. **If it fails** S2; S3 for the sentences.

**A-83 · Three fuzz targets, the soak, every prior suite, and the A4 product verdict**
☐ **Steps** (a) Configure `portable-fuzz` with a fixture declaring two targets. (b) The `portable-fuzz` job and `fuzz-soak.yml` over three targets — **read the logs**. (c) `ctest --preset x64-release` and the `gates` preset. (d) Assemble A-57 with M-03's product walk.
**Expect** (a) Configure **fails** on the wrong count; three builds and links. (b) `fuzz_cos_roundtrip` runs seeded, ten minutes clean on all three, no `runtime error:` line. (c) Every case that existed at Stage 2's sign-off still passes **by name**, except the two A-81 moved, which are named; the counts per prefix recorded. (d) **A4's product verdict recorded** in `Tech-Stack-Plan.md` §3.3 beside the probe's.
**If it fails** S1 for a lost prior case or an unrecorded verdict; S2 otherwise.

---

## 6. Manual verification — you run this

Eight cases. Every one needs a validator that cannot be scripted here, an independent reader on real
glass, the real product killed at your moment, or a dialog met where work is at stake. **Anything here
that could have been automated is a defect in this guide — say so and I will move it.**

**Walk M-03's probe walk as soon as T2 asks for it** — it is the go/no-go, and every other case waits on
the stage surviving it. The rest are walkable from T11 (M-04 needs the recovery prompt) and T12 (M-05
and M-06 need the settings).

Two things about the state you will be walking, so a planned absence is not reported as a defect:

- **The Rotate Pages dialog has two page choices** — current and all. Ranges are Stage 7.
- **Save As copies and appends, like Save.** A Save-As file is as large as the original plus the edit.

---

**M-01 · The first mutation, by hand** ☐
- **Goal** The whole of flow D as a person meets it: chord, dialog, ribbon, undo, redo, and the page on
  screen agreeing with the model at every step.
- **Pre** Open `corpus\ordinary\mixed-page-sizes.pdf` — portrait and landscape pages — in
  `SingleContinuous`.
- **Steps**
  1. On page 1, press `Ctrl+R`; choose 90° clockwise, *current page*; OK.
  2. Press `Ctrl+Z`, `Ctrl+Y`, `Ctrl+Z`, `Ctrl+Shift+Z` — watching the page each time.
  3. `Ctrl+R` again; 180°, *all pages*. Then **one** `Ctrl+Z`.
  4. Rotate page 2 from **Organize → Arrange → Rotate** on the ribbon. Undo it from **Home → Clipboard → Undo**.
  5. Scroll through the document after each step; zoom to 400 % on a rotated page.
- **Expect** Every rotation appears in place, crisp, with **no flash of the unrotated page that
  persists**; the pages below move to make room and nothing overlaps. Step 3's single undo reverts
  **every** page. The tab title carries the unsaved marker from step 1 and loses it when you undo back
  to the start. Undo's tooltip names the rotation.
- **If it fails** S2; S1 if the page shown disagrees with what undo/redo says the document is.

**M-02 · Save, reopen, and three readers that are not us** ☐
- **Goal** A-58 proves PDFium reads our file; only other engines on real glass show that nothing
  about the update is RawPage-specific.
- **Pre** P-1. Copy `corpus\ordinary\rotated-pages.pdf` and `corpus\ordinary\hybrid-xrefstm.pdf` to a
  scratch folder. For the second file, **download** it from a local share or through the browser so it
  carries *Mark of the Web* — Explorer → Properties shows *"This file came from another computer"*.
- **Steps**
  1. Open the copy of `rotated-pages.pdf`; rotate pages 1 and 3; `Ctrl+S`. Close RawPage.
  2. Reopen it in RawPage. Then open it in **Acrobat**, **SumatraPDF** and **Firefox** (right-click →
     *Open with*).
  3. Repeat with the downloaded `hybrid-xrefstm.pdf`. After saving, open its Explorer Properties.
- **Expect** All four readers show the **same** rotations, and none reports damage or offers to repair.
  Acrobat does not ask to save on close (it would if it had repaired the file). The file stays where it
  was, with its name. **The downloaded file still says it came from another computer** — D3-4's
  `ReplaceFileW` kept its stream.
- **If it fails** S1 if any reader cannot open the file or shows it damaged; S2 for a lost *Mark of the
  Web*; S2 for a rotation that differs between readers.

**M-03 · A4 in Acrobat — the probe walk (T2) and the product walk (T13)** ☐ probe · ☐ product
- **Goal** The policy half of A4 (D3-2), which OpenSSL cannot judge — and a control that proves Acrobat
  is judging.
- **Pre** P-1. **Probe walk:** the four files A-28 wrote, which I will name in the session. **Product
  walk:** copies of the four `corpus\signed\` files, each opened in RawPage, **page 1 rotated 90°**,
  saved with `Ctrl+S`.
- **Steps** For each file, open it in Acrobat and open the **Signatures** panel. Expand each signature.
  For the approval signatures, use *View signed version*.
- **Expect**
  - `approval.pdf`, `approval-xrefstream.pdf`, `approval-then-update.pdf`: each signature **valid**;
    Acrobat may say the document was changed **after** signing, and lists the later revision — that is
    correct for an approval signature and is not a failure. *View signed version* shows page 1
    **unrotated**.
  - **`certified-nochanges.pdf` — the control: Acrobat must report the certification as invalidated,
    or the change as not permitted.** If Acrobat calls this file fine, the other three results mean
    nothing, and the walk is reported as *unjudged*, not passed.
- **If it fails** **S1 and, on the probe walk, the stage's no-go** for an approval signature reported
  invalid for a reason in the bytes. S2 for a reason in the update's shape (§3's middle row). A control
  that passes voids the walk.

**M-04 · Kill the real product mid-edit, and recover** ☐
- **Goal** A-68 kills a probe; this kills the product, and meets the dialog a person meets.
- **Pre** P-5. A copy of `corpus\ordinary\text-letter.pdf` open in RawPage.
- **Steps**
  1. Rotate page 1; then *all pages* 180°; then undo once and rotate page 2. **Do not save.**
  2. End the process in Task Manager. Relaunch RawPage with no arguments.
  3. Choose **Recover**. Press `Ctrl+Z` twice.
  4. Repeat steps 1–2, but before relaunching **copy another PDF over the file**. Choose the offered
     *recover to a new file*.
  5. Repeat steps 1–2 once more and choose **Discard**. Relaunch again.
- **Expect** Step 2: the dialog names the document. Step 3: the document is exactly as it was before
  the kill — page 1 rotated, the all-pages rotation undone, page 2 rotated — **marked unsaved**, and
  undo walks back through the same history. Step 4: an **untitled** document appears with a banner
  saying the source had changed; `Ctrl+S` asks for a path. Step 5: nothing offered on the second
  relaunch.
- **If it fails** S1 for any lost or wrong change; S2 for a confusing dialog.

**M-05 · A recovery directory you cannot write** ☐
- **Goal** `Tech-Stack-Plan.md` §5.5's fallback, as a person sees it.
- **Pre** Create `C:\ro-recovery`; deny yourself *Write* on it (Properties → Security). Set
  **Preferences → Documents → recovery directory** to it.
- **Steps** Open any corpus file; rotate five times; save; rotate again; kill the process; relaunch.
- **Expect** **One** banner, at the first rotation, saying recovery is unavailable; no further banner;
  editing and saving work. After the kill, **nothing is offered** — which is what the banner said.
- **If it fails** S2; S1 if editing is refused.

**M-06 · A repaired file's first save** ☐
- **Goal** D3-6, end to end, with a reader that repairs silently in the comparison.
- **Pre** P-1. A copy of `corpus\malformed\xref-offsets-short.pdf` (its banner says it was repaired).
- **Steps**
  1. Rotate page 1; `Ctrl+S`. Read the explanation; choose a new file name.
  2. Reopen the new file in RawPage. Open it in Acrobat.
  3. Set **Preferences → Documents → warn on repaired save** off; repeat step 1 on a fresh copy.
- **Expect** Step 1: an explanation, then a Save-As dialog — **never** a silent in-place save; the
  original copy is unchanged. Step 2: **no repair banner** in RawPage; Acrobat opens it without asking
  to save on close. Step 3: **no explanation — and still Save-As.**
- **If it fails** S1 for an in-place save; S2 for a new file that still reads as repaired.

**M-07 · Closing with unsaved changes** ☐
- **Goal** D3-14 on every path a person closes by.
- **Pre** Two corpus files open, both rotated, neither saved.
- **Steps** Close a tab with its ×, and with `Ctrl+W` — *Cancel*. Tear one tab into its own window and
  close that window — *Discard*. `Ctrl+Q` — *Save* for the remaining one.
- **Expect** Every route asks, naming the document; *Cancel* leaves it open and unsaved; *Discard*
  closes it and it is **not** offered for recovery next launch; *Save* saves then quits. **Nothing
  closes without asking.**
- **If it fails** S1.

**M-08 · Saving while something else holds the file** ☐
- **Goal** A-48 plants a handle; a real application's sharing mode is the thing only a real application has.
- **Pre** P-1. A copy of `corpus\ordinary\text-a4.pdf` open in **both** RawPage and Acrobat.
- **Steps** Rotate in RawPage; `Ctrl+S`. Then close Acrobat and `Ctrl+S` again. Then set the file
  **Read-only** in Explorer and save once more.
- **Expect** With Acrobat open: either the save succeeds and Acrobat, when refocused, keeps working, or
  RawPage says the file is in use and offers Save As — **in both cases the file is never partial**, and
  Acrobat never shows it damaged. After closing Acrobat: the save succeeds. Read-only: a message and a
  Save-As offer; the file unchanged.
- **If it fails** S1 for a damaged file; S3 for an unclear message. **Record which of the two outcomes
  happened with Acrobat open** — it is a fact about Acrobat's sharing mode that nothing here can derive.

---

## 7. Known caveats — verify, do not assume broken

Behaviours that look like defects and are not. **A wrong row here is worse than a missing one.**

| Looks broken | Is |
|---|---|
| **Acrobat says an approval-signed document "has been changed since it was signed"** after a RawPage save | Correct — there **is** a later revision, and an approval signature permits one. The finding would be *signature invalid*, not *changed after signing*. M-03 |
| **Acrobat flags the certified sample after a rotation** | Correct and required — it is M-03's control. RawPage honouring `/DocMDP` is Stage 11 |
| **A saved file is the original's size plus a little** | Correct — an incremental update is kilobytes |
| **A Save-As file is as large as the original, not compacted** | Correct at Stage 3 (D3-16). Stage 8 |
| **Save is disabled on a document you just opened** | Correct — nothing to save, and its tooltip says so |
| **Rotate is disabled on an encrypted document** | Correct (D3-7). Stage 10 |
| **`Ctrl+R` in a Thumbnails pane does nothing** | There is no Thumbnails pane (Stage 4). The binding exists and a case proves it resolves |
| **After a Save, undo is still available** | Correct — undo history survives a save, and undoing past the save point makes the document dirty again |
| **Undoing back to the saved state clears the unsaved marker, but undoing further does not return it to clean** | Correct (D3-10) — only the saved position is clean |
| **A `.rawpage-<pid>.tmp` file appears beside a document for a moment while saving** | Correct (D3-4). One left behind **after** RawPage has exited is S3; the next save removes it |
| **RawPage relaunched after a crash offers recovery, but a normal quit never does** | Correct — a normal quit asks about unsaved documents (D3-14) instead |
| **`/ModDate` in Document Properties (in another reader) is unchanged after saving** | Correct at Stage 3 (D3-5). Stage 4 or 8 |
| **The recovery directory has a `lock` file** | Correct — it is how a running instance's journal is never offered as a crashed one |
| **Edge or Chrome opens the saved file fine** | Expected and not evidence — both are PDFium, which A-58 already checks |

---

## 8. Issue log

Numbered `I-nn`, **this stage's own namespace**.

| # | Sev | Title | State |
|---|---|---|---|
| **I-01** | S3 | **`Tech-Stack-Plan.md` §5.3 declared `Command::name()` as a `QString` that *"goes through `tr()`"*, which `core/` cannot build.** `core/` has no `tr()` and no `QObject` (`core/Result.h`'s own header), and convention 5 makes the catalog key the source string, so user-facing English in `core/` is the thing the design forbids. Found while writing this guide's A-42, by reading the snippet against `core/Result.h` — the same shape as Stage 2's I-03, three signatures that named `QImage` in a layer that may not. **Corrected in the same change** to return a catalog key and arguments, which `RepairReport::summaryKey()` already does; recorded as plan decision **D3-10**(a) | ✅ **Closed 2026-09-14**, before any code |
| **I-02** | S4 | **Flow D (`Architecture-FileMap.md` §5) dispatched `ActionId::RotatePages`, an id that does not exist.** `core/cmd/ActionId.def`'s row is `PageRotate` / `page.rotate`, default context `PageThumbnails`, bound in both `Viewer` and `PageThumbnails` — confirmed with `rawpage.exe --list-actions` on 2026-09-14. The trace also showed a `Permissions::may(Assemble)` operation and `PageOpsDialogs::rotate()`, both of which this plan renames (D3-7, D3-13). Found by reading the trace against the table while writing §5.11. **Corrected in the same change** | ✅ **Closed 2026-09-14**, before any code |

**NO S1 OR S2 IS OPEN.**

---

## 9. Sign-off criteria

Stage 3 is done when **all** of these hold — [`Stage3-Mutation-Plan.md` §13](./Stage3-Mutation-Plan.md#13-definition-of-done-for-stage-3)
restated as a checklist.

1. ☐ A-01 … A-83 **run**, with actual output recorded against each case — not a summary claiming they
   were (Stage 1's I-32). Linux-only halves (A-04, A-26, A-45's and A-70's sanitizer runs, A-83's fuzz
   logs) are recorded from the CI job's **log**, not its tick (rule 3).
2. ☐ M-01 … M-08 **walked and signed off by you**, and M-03 walked **twice** — probe and product. I do not
   mark a manual case.
3. ☐ `checks` clean — **four** gates, `mutation-lint`'s fixtures failing for their own rules and its
   control passing.
4. ☐ `ctest` green across all five Windows targets and the `gates` preset, every prior stage's case
   present by name (A-83).
5. ☐ `portable-asan` clean including the writer, the sink, the round trip and the journal parser;
   `portable-fuzz` and the ten-minute soak clean on **three** targets.
6. ☐ **Golden suite green with its 198 baselines unchanged, and the round-trip suite green** — the first
   stage where both exist.
7. ☐ **A4 has a recorded verdict, probe and product**, in `Tech-Stack-Plan.md` §3.3, graded on §3's scale
   **fixed before the measurement**, and stated as holding **over RawPage's generated signatures** (§1).
8. ☐ **The A4 bound is written into the Stage 3 status block in those words**, as Stage 2's corpus limit
   was — an unqualified *"signatures survive a save"* overstates the evidence.
9. ☐ No open S1 or S2 in §8.
10. ☐ `Architecture-FileMap.md` rows flipped and the plan's §10 debt paid; `Implementation-Plan.md`'s
    Stage 3 status block updated to reality, deviations included.
