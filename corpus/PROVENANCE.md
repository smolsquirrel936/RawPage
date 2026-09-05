# Corpus provenance

**Every file in `corpus/` was produced by a script in `corpus/`. Nothing here was downloaded,
borrowed, curated or hand-edited.** That is decision **D2-11**
([`claude-plan/Stage2-Rendering-Plan.md`](../claude-plan/Stage2-Rendering-Plan.md) §3), taken on
2026-08-31, and this file is the record it requires: one row per file, naming the generator that
produced it and what it is for — and, for `malformed/`, exactly what is wrong with it.

Three things fall out of that decision, and all three are the reason this file exists rather than a
directory listing:

- **Every file regenerates byte-identically from a clean checkout.** A PDF cross-reference table
  stores *absolute byte offsets*, so a one-character edit invalidates every offset below it and the
  result is a file some readers repair silently and others reject — which makes it useless as a
  fixture, because you no longer know which path you are testing. Reproducibility is **asserted**,
  not assumed: `python corpus/make-corpus.py --verify`, run by CTest as
  `gates.corpus.reproducible`.
- **No committed file needs licence clearance.** `tools/license-audit` governs *linked
  dependencies* and says nothing about data, so a borrowed PDF would sit in a gap no gate in this
  project closes.
- **The damage in `malformed/` is described rather than discovered.** *“the xref offsets are 12
  bytes short from object 4 onward”* is a test; *“this file is broken somehow”* is a mystery that
  gets re-diagnosed every time it fails.

## What this corpus cannot tell you

**A generator emits only what its author already understood.** `tests/golden/` therefore proves we
rasterize **our own files consistently**; it does **not** prove we rasterize **real-world PDFs
correctly**. Real-producer constructs — a scanner's CCITT quirks, InDesign transparency, an unusual
colour space, a broken-but-tolerated producer string — are outside Stage 2's evidence entirely.
This is recorded as a known limitation in `Stage2-Rendering-Plan.md` §12 and as a Definition-of-Done
item in `Stage2-Rendering-Testing-Guide.md` §13.6, and **the trigger for revisiting D2-11 is the
first real-world rendering bug this corpus cannot reproduce.**

Three narrower limits, each stated where it is created rather than left to be inferred:

| Limit | Where it comes from |
|---|---|
| **The JPEG 2000 stream carries one coding pass.** `image-jpx.pdf` decodes to a two-tone approximation — a 0/255 source comes back as roughly 0/128 — because `jpxenc.py` implements the MQ coder and the EBCOT **cleanup** pass and not the significance-propagation or magnitude-refinement passes. It is a *truncated* codestream, not a malformed one, and it exercises the `/JPXDecode` **path**; it is not a claim about JPEG 2000 fidelity | `corpus/jpxenc.py` |
| **The JPEG streams are baseline 4:4:4 only.** No progressive, no chroma subsampling, no arithmetic coding, no restart markers. Each of those is a genuinely different decode path | `corpus/jpegenc.py` |
| **The embedded fonts are nine hanzi and seven capitals, all built from rectangles.** They are *real characters correctly drawn* — 一 二 三 十 口 日 田 中 目 and E F H I L T — chosen because those glyphs genuinely are straight strokes. No curves, no hinting, no kerning, no composite glyphs. What is exercised is the embedding path (`/FontFile2`, CIDFontType2, Identity-H, `/W`), not typographic fidelity | `corpus/fontenc.py` |

## How to regenerate

```powershell
python corpus/make-corpus.py                    # write every file
python corpus/make-corpus.py --verify           # regenerate in memory, compare against disk
python corpus/make-corpus.py --check-provenance # every file has a row here; every row has a file
python corpus/make-corpus.py --list             # every file with its size and sha256
```

Individual generators (`make-ordinary.py`, `make-malformed.py`, `make-encrypted.py`) can be run on
their own and write only their own directory. `make-minimal.py` and `make-a2-text-page.py` predate
this task and are deliberately **not** ported onto `pdfbuild.py`: `minimal.pdf`'s hash is quoted in
`Stage0-Toolchain-Testing-Guide.md` and `a2-text-page.pdf` is the A2 probe's named input in
`Tech-Stack-Plan.md` §3.3, so rewriting either would change bytes those documents cite, for nothing.

## The generators and the encoders they use

| File | What it is |
|---|---|
| `corpus/make-corpus.py` | The driver: regenerates everything, and **asserts** byte-identical regeneration and provenance completeness |
| `corpus/pdfbuild.py` | Deterministic PDF assembly — objects, classic xref, xref streams, object streams, incremental revisions, the filter helpers, page-tree furniture |
| `corpus/deflate.py` | A DEFLATE/zlib encoder we own, emitting **stored, fixed and dynamic** Huffman blocks. Not `zlib.compress`, whose output depends on the zlib build the interpreter links — which would make the same script emit different bytes on Windows and on the Ubuntu runner |
| `corpus/jpegenc.py` | Baseline JPEG (ITU-T T.81), for `/DCTDecode`. Hardcoded DCT cosine table rather than `math.cos`, whose last bit is the platform's libm |
| `corpus/ccitt.py` | CCITT Group 3 (T.4) and Group 4 (T.6), for `/CCITTFaxDecode`, with an independent decoder in the same file as the self-check |
| `corpus/jpxenc.py` | JPEG 2000: MQ arithmetic coder, EBCOT cleanup pass, tag trees, packet headers. See the limitation above |
| `corpus/fontenc.py` | Synthetic TrueType fonts (`glyf`/`loca`/`cmap`/`hmtx`/`head`/`hhea`/`maxp`/`name`/`post`/`OS/2`) |
| `corpus/cryptenc.py` | The PDF standard security handler, encrypt side: RC4, AES-128/192/256, revisions 2/3/4/6. **Test-data only** — `pdf/cos/StandardSecurityHandler` (Stage 10) is the real one and shares no code with it |

Each encoder **self-checks against an independent implementation** before its output reaches a
corpus file: `deflate.py` round-trips through `zlib.decompress`, `ccitt.py` through its own
decoder, `cryptenc.py` against the FIPS-197 known-answer vectors, and — opportunistically, when the
package happens to be importable — `jpegenc.py`, `jpxenc.py` and `fontenc.py` through Pillow
(libjpeg, OpenJPEG and FreeType). **Pillow and `cryptography` are not dependencies of this
project**; when they are absent the self-check says so rather than passing quietly.

---

## Seed files

| File | Generator | What it is for |
|---|---|---|
| `minimal.pdf` | `make-minimal.py` | Stage 0's single seed file: one valid page, no compression, no embedded font. Its hash is quoted in `Stage0-Toolchain-Testing-Guide.md` as the `.gitattributes` binary-handling check |
| `a2-text-page.pdf` | `make-a2-text-page.py` | The **A2 probe's** input (Stage 2 T3, `Tech-Stack-Plan.md` §3.3): a dense A4 page, 62 text runs, 6,650 glyphs of 9 pt Helvetica, two hairlines, uncompressed content stream. Its docstring states what the page deliberately is *not*, because the A2 verdict inherits every one of those limits |

## `ordinary/` — the files that are supposed to open and render

| File | Generator | What it is for |
|---|---|---|
| `ordinary/text-a4.pdf` | `make-ordinary.py` | Three A4 pages of body copy. **Page 1's content stream is uncompressed on purpose** — otherwise no file in the corpus can be read in a hex editor, and that is the first thing anyone does with a rendering bug |
| `ordinary/text-letter.pdf` | `make-ordinary.py` | US Letter rather than A4. A different `/MediaBox` is the cheapest thing that breaks a layout engine that assumed one page size |
| `ordinary/mixed-page-sizes.pdf` | `make-ordinary.py` | A4 portrait, A4 landscape, A5, and a page whose `/MediaBox` has a **non-zero origin** — legal, common in imposed print files, and the case a layout that treats the box's width as its right edge gets wrong |
| `ordinary/rotated-pages.pdf` | `make-ordinary.py` | `/Rotate` 0, 90, 180, 270 over **identical** content, so the only difference between the four rendered pages is the rotation |
| `ordinary/vector-hairlines.pdf` | `make-ordinary.py` | A width ladder from 0 to 2 pt, a 0.1 pt grid and 0-width diagonals. The file `pageDisplay/lineWeight` (T15) is judged against, and the one that shows whether a hairline survives a tile boundary |
| `ordinary/vector-curves.pdf` | `make-ordinary.py` | Cubic Béziers, **both winding rules over the same path** (`f` and `f*`), dash patterns, line joins and caps |
| `ordinary/shading-patterns.pdf` | `make-ordinary.py` | Shading types 2 (axial), 3 (radial) and 1 (function-based), driven by a type 2 exponential **and** a type 0 sampled function — the sampled one carries a stream, a `/Size` and a `/BitsPerSample`, which nothing else in the corpus does |
| `ordinary/tiling-pattern.pdf` | `make-ordinary.py` | Type 1 tiling patterns, coloured (`/PaintType 1`) and uncoloured (`/PaintType 2`). The uncoloured one is painted **twice in two different colours**, which a renderer that caches a pattern's rasterization by object gets wrong |
| `ordinary/transparency-groups.pdf` | `make-ordinary.py` | Constant alpha, four blend modes, an isolated non-knockout transparency group XObject, and a **luminosity soft mask** — four separate branches in a compositor, and a page using only `ca` would leave three untested |
| `ordinary/image-dct-rgb.pdf` | `make-ordinary.py` + `jpegenc.py` | Baseline JPEG, 3 components, 4:4:4, `/DCTDecode`. Smooth gradients (DC path), a checker (AC path) and a hard edge (where ringing shows) |
| `ordinary/image-dct-gray.pdf` | `make-ordinary.py` + `jpegenc.py` | Baseline JPEG, 1 component — a different SOF/DHT arrangement from the RGB file, not a smaller version of it |
| `ordinary/image-jpx.pdf` | `make-ordinary.py` + `jpxenc.py` | `/JPXDecode`. **One coding pass — read the limitation above before judging the rendered result** |
| `ordinary/image-ccitt-g4.pdf` | `make-ordinary.py` + `ccitt.py` | CCITT Group 4 (T.6), `K=-1`, `/BlackIs1 true`. Two-dimensional coding against a reference line — pass, horizontal and vertical modes all occur |
| `ordinary/image-ccitt-g3.pdf` | `make-ordinary.py` + `ccitt.py` | CCITT Group 3 one-dimensional (T.4), `K=0`. Same bitmap as the G4 file, a completely different decoder path |
| `ordinary/image-flate-predictor.pdf` | `make-ordinary.py` | `/FlateDecode` with a **PNG Up predictor** and a dynamic-Huffman block. A decoder that ignores `/DecodeParms` produces a visibly wrong image rather than failing |
| `ordinary/image-indexed-stencil.pdf` | `make-ordinary.py` | An `/Indexed` palette image, a 1-bit `/ImageMask` stencil painted in the current colour, and an 8-bit `/SMask` alpha channel |
| `ordinary/image-lzw-ascii85.pdf` | `make-ordinary.py` | A filter **chain** — `[/ASCII85Decode /LZWDecode]` — plus `/RunLengthDecode`, an `/ASCIIHexDecode` content stream, and a `/Contents` **array**. A reader that handles one filter per stream passes every single-filter fixture in this corpus |
| `ordinary/cjk-embedded.pdf` | `make-ordinary.py` + `fontenc.py` | An embedded CJK subset: CIDFontType2, Identity-H, `/FontFile2`, `/CIDToGIDMap /Identity`, `/W`. Identity-H means the string bytes **are** glyph indices — the path with no encoding lookup at all, which is what producers actually emit for CJK. Nine hanzi at four sizes, for glyph-cache pressure |
| `ordinary/latin-embedded.pdf` | `make-ordinary.py` + `fontenc.py` | The **control** for the file above: same embedding mechanism, simple (non-composite) `/TrueType`, single-byte codes, a real `/Widths` array. A failure in one and not the other localizes immediately |
| `ordinary/font-not-embedded.pdf` | `make-ordinary.py` | **Two fonts the document names and does not embed** — one serif, one sans, differing in exactly one `/Flags` bit — each with a `/FontDescriptor`, a `/Widths` array and **no `/FontFile` of any kind**. Added at **S2 · T16**, because none of the other 29 could serve guide case **A-66**: every other text fixture uses either a base-14 face, which every conforming reader is obliged to have and PDFium answers from its own bundled copies, or an embedded `/FontFile2`, where the outlines are in the file. A missing font is the third thing and the only one that reaches a font resolver. The `/BaseFont` names are `RAWPGA+RawPageAbsentSerif` and `RAWPGB+RawPageAbsentSans` — ours, installed nowhere, so the substitute can never accidentally be the requested face — and each carries a six-capital **subset tag with nothing subset**, which is what a real producer leaves behind when it subsets and then fails to embed |
| `ordinary/forms-acroform.pdf` | `make-ordinary.py` | The form-heavy page: single-line, multiline and password text fields, two check boxes, a three-button radio group, a combo box, a list box and a push button — **every one with a real appearance stream**, because a widget without `/AP` renders as nothing and would make the fixture look correct while proving the opposite |
| `ordinary/annots-mixed.pdf` | `make-ordinary.py` | Nine annotation types with appearance streams — link (with a `/Dest` into page 2), text note, square, circle, line, highlight, underline, strikeout, free text. Stage 6 owns annotations; the file exists now because a page that renders its content and silently drops `/Annots` looks correct |
| `ordinary/tagged.pdf` | `make-ordinary.py` | `/StructTreeRoot`, marked-content sequences, `/MarkInfo`, `/Lang`, and a **drawing order that is not the reading order**. Not an accessibility fixture — accessibility left this product on 2026-08-23 (`Features.md`); `core/doc/TagTree` survives as Write mode's structure under decision O2, and Stage 13 needs this document |
| `ordinary/outline-labels.pdf` | `make-ordinary.py` | A two-level document outline and `/PageLabels`: roman front matter then arabic body, so **the fourth page is called “1”** — which is why the status bar cannot print `index + 1` |
| `ordinary/cover-odd-count.pdf` | `make-ordinary.py` | Seven pages with a cover, `/PageLayout /TwoPageRight`. The spread-parity case: cover alone, then 2-3, 4-5, 6-7. **With an even page count the off-by-one is invisible**, which is why the count is odd |
| `ordinary/rtl-binding.pdf` | `make-ordinary.py` | `/ViewerPreferences /Direction /R2L`. RTL binding changes which side of a spread a page sits on and nothing else — so a per-page golden image cannot catch a left-to-right assumption and the fixture has to be the whole document |
| `ordinary/objstm-xrefstream.pdf` | `make-ordinary.py` | PDF 1.5 structure: objects packed into an `/ObjStm`, a cross-reference **stream** with `/W` field widths and type-2 entries. A completely separate reader path from the classic table |
| `ordinary/incremental-3gen.pdf` | `make-ordinary.py` | Three generations appended, not rewritten — what `tst_xreftable` (A-20) walks. Gen 2 replaces page 2 and adds page 3; gen 3 replaces page 1 and the `/Info`. **The earlier bytes are still there verbatim**, which is the only way to catch a reader that rebuilds instead of following `/Prev` |
| `ordinary/pages-900.pdf` | `make-ordinary.py` | 900 pages in a balanced tree of 30 × 30, with object streams. A flat 900-entry `/Kids` array is legal and is exactly the shape that hides an O(n) page lookup behind a fast small document |
| `ordinary/stream-length-control.pdf` | `make-ordinary.py` | The **control** for `malformed/length-lies-short.pdf`. A-17 asks for a lying `/Length` *and* a correct one, because the second is what proves the recovery path actually ran. The damaged twin is built by importing this generator, so the pair cannot drift apart — they differ in exactly three bytes |

## `malformed/` — one named fault per file

**One damage per file is a rule, not a style.** A file with two faults cannot tell you which one
your repair path handled, and the exit criterion these exist for — **A-21**, *every* file yields a
report and never a crash — is only meaningful if a report can be checked against a known cause.
Each is a valid document from the same builder with exactly one thing done to it.

| File | The fault, exactly | Why it is worth a file |
|---|---|---|
| `malformed/xref-offsets-short.pdf` | Every xref offset **from object 4 onward is 12 bytes too small** | D2-11 names this shape explicitly. What a producer that edited in place and forgot to rewrite the table leaves behind. Recoverable: the objects are intact and in order |
| `malformed/xref-missing.pdf` | The `xref` keyword and all its entries are **gone**; the trailer and `startxref` remain and point where the table was | The whole table must be rebuilt by scanning for `N 0 obj` — `XrefRepair`'s widest path, and the *“reconstructed the entire xref”* extreme A-22 asks for |
| `malformed/startxref-into-object.pdf` | `startxref` points **into the middle of object 3** | Distinct from a missing table: the table is there and correct. Recovery means searching backwards for it, which is a different repair from rebuilding |
| `malformed/startxref-past-eof.pdf` | `startxref` points **4,096 bytes past the end of the file** | What a truncated download leaves when the trailer survived in a cache. A reader that seeks there and reads must not fault |
| `malformed/circular-prev.pdf` | An incremental file whose second revision's **`/Prev` points at itself** | The termination case for the `/Prev` walk. A reader without a visited set **hangs**, and A-21 forbids a hang as well as a crash |
| `malformed/free-list-broken.pdf` | The free-list head points at **object 9, which is in use**, and object 9's entry is still marked `n` | A reader that walks the free list to allocate walks into live data |
| `malformed/size-too-small.pdf` | The trailer says **`/Size 4`** in a file of a dozen objects | Everything above index 4 is, by the trailer's own account, not there — while the table lists it and the objects exist |
| `malformed/length-lies-short.pdf` | A content stream's **`/Length` is 12 bytes short** | The most common real-world malformation there is. Recovering means finding `endstream` rather than trusting the dictionary. Its control is `ordinary/stream-length-control.pdf`; the two differ in three bytes |
| `malformed/length-lies-long.pdf` | The same stream's **`/Length` is 400 bytes too long**, so it runs off the object's end | The mirror image, and a genuinely different repair: too short leaves data behind, too long consumes structure |
| `malformed/truncated-mid-stream.pdf` | The file **stops mid-stream** — no `endstream`, `endobj`, xref, trailer or `%%EOF` | An interrupted download: the single most common damaged file a reader is handed |
| `malformed/no-eof-marker.pdf` | Everything intact **except the trailing `%%EOF`** | Readers that scan backwards for it from the end find nothing and must fall back rather than give up |
| `malformed/junk-before-header.pdf` | **1,024 bytes of HTTP-response-shaped junk before `%PDF-`** | Every offset is now wrong by exactly 1,024 — the recoverable form, because it is off by a constant a reader can measure. Sits right at ISO 32000's 1,024-byte header allowance on purpose |
| `malformed/missing-endobj.pdf` | Object 5's **`endobj` is gone** | A lexer that requires `endobj` swallows object 6; a tolerant one notices `6 0 obj`. `pdf/cos/Lexer` is specified as tolerant by design, and this file says what that means |
| `malformed/duplicate-object-number.pdf` | **Two objects numbered 5**, at different offsets, with the xref naming the first | Real files acquire this through a bad merge. The rule — *the xref's entry wins, not the last one in the file* — is what a reader that rebuilds by scanning gets wrong |
| `malformed/bad-flate-stream.pdf` | Two bytes in the middle of a `/FlateDecode` stream are **corrupted** | The Adler-32 will not match and the Huffman decode fails partway. The question is whether the reader keeps what it decoded; nothing else in the file is wrong, which is what makes the answer visible |
| `malformed/unbalanced-dictionary.pdf` | A page dictionary's **closing `>>` is missing** | A lexer with no depth limit reads the rest of the file as one dictionary |
| `malformed/bad-name-hex-escape.pdf` | A name containing **`#GG`** — a `#` escape whose two digits are not hexadecimal | ISO 32000 says nothing about what to do here. Every reader invents an answer; ours should be a **named** one rather than an accident |
| `malformed/deep-nesting.pdf` | An array nested **4,000 levels deep** | Legal PDF — there is no depth limit — and fatal to an unguarded recursive-descent parser, which blows the stack. That is a **crash**, which A-21 forbids, so the guard is not optional |
| `malformed/huge-length-claim.pdf` | A stream declaring **`/Length 4294967295`** in a file of a few thousand bytes | The unbounded-allocation case: four gigabytes reserved for a two-kilobyte file. A-21's *“no unbounded allocation”* clause |
| `malformed/pages-count-wrong.pdf` | `/Pages` declares **`/Count 97`** over three real kids | The page count goes in the status bar before the tree has been walked. This file decides whether the tree or the number is authoritative — and the answer has to be the tree |
| `malformed/pages-loop.pdf` | A page tree with a **cycle**: an intermediate `/Pages` node lists its own ancestor as a kid | Walking it without a visited set does not terminate. Like `circular-prev.pdf`, catches a hang rather than a crash |
| `malformed/no-root.pdf` | The trailer has **no `/Root`** | Every object is intact and there is no way in without finding the `/Type /Catalog` by scanning — which is a **recovery**, and should be reported as one rather than performed silently |
| `malformed/root-points-at-nothing.pdf` | **`/Root 97 0 R`** in a file whose highest object number is far below 97 | Different from a missing `/Root`: here the trailer is confidently wrong, and a reader that resolves a dangling reference to null gets a document with no catalog **and no error** |
| `malformed/objstm-corrupt.pdf` | An object stream's **`/First` offset is 8 bytes wrong**, so every packed object is read from the wrong place | Nothing in the classic-xref half of the reader touches this path, which is why it needs its own damaged fixture rather than being assumed covered |

## `encrypted/` — genuinely enciphered, with public passwords

**The passwords are fixed and public: user `user`, owner `owner`.** There is nothing to protect —
these are fixtures — and a fixture whose password is a secret is a fixture nobody can use.

Under **D2-6** these files have two readers with two different correct answers: PDFium opens them
through its password callback, and the first-party COS reader **declines them by name** until
`StandardSecurityHandler` arrives at Stage 10. Both halves are only testable against bytes that are
actually enciphered — a file carrying an `/Encrypt` dictionary over plaintext would let the decline
look correct while PDFium silently failed.

| File | Handler | What it is for |
|---|---|---|
| `encrypted/rc4-40.pdf` | R2 / V1 | RC4, 40-bit key. The original standard security handler, still emitted by anything predating Acrobat 5 |
| `encrypted/rc4-128.pdf` | R3 / V2 | RC4, 128-bit key, with the 50-round key derivation that arrived with revision 3 |
| `encrypted/aes-128.pdf` | R4 / V4 | AES-128 through a named crypt filter (`/StdCF`, `/CFM /AESV2`) — revision 4 is where crypt filters appear |
| `encrypted/aes-256.pdf` | R6 / V5 | AES-256 (`/AESV3`), the ISO 32000-2 handler. **The only one not built on MD5**: Algorithm 2.B's iterated SHA-2/AES password hash, the file key wrapped in `/UE` and `/OE`, and no per-object key derivation at all. A reader that handles R2–R4 shares almost no code with one that handles this |
| `encrypted/owner-empty-user.pdf` | R4 / V4 | AES-128 with an **empty user password** and printing denied (`/P -3904`). It opens with no prompt, which is what makes it worth having: a reader that treats *“no password needed”* as *“not encrypted”* gets every byte wrong and nothing on screen says so |

**Not here, and why.** No signed document — signing is Stage 10 and risk A4 belongs to Stage 3's
round-trip suite, so a `corpus/signed-*.pdf` produced now would be a fixture with nothing to check
it. No public-key (`/Filter /Adobe.PubSec`) encryption: it needs a certificate, a certificate needs
a private key, and a private key committed to a repository is a bad habit even when it protects
nothing.
