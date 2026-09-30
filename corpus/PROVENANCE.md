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

Seven narrower limits, each stated where it is created rather than left to be inferred:

| Limit | Where it comes from |
|---|---|
| **The JPEG 2000 stream carries one coding pass.** `image-jpx.pdf` decodes to a two-tone approximation — a 0/255 source comes back as roughly 0/128 — because `jpxenc.py` implements the MQ coder and the EBCOT **cleanup** pass and not the significance-propagation or magnitude-refinement passes. It is a *truncated* codestream, not a malformed one, and it exercises the `/JPXDecode` **path**; it is not a claim about JPEG 2000 fidelity | `corpus/jpxenc.py` |
| **The JPEG streams are baseline 4:4:4 only.** No progressive, no chroma subsampling, no arithmetic coding, no restart markers. Each of those is a genuinely different decode path | `corpus/jpegenc.py` |
| **Every signature is one generated signer.** RSA-2048, PKCS#1 v1.5, SHA-256, a self-signed certificate, signed attributes `contentType`, `signingTime` and `messageDigest` only — no timestamp, no revocation data, no PAdES profile, no visible appearance. Risk A4's verdict is therefore bounded to *"over RawPage's generated signatures"* (`Stage3-Mutation-Testing-Guide.md` §1); a signature from another producer may differ in exactly the way that matters, and the first real signed file that does not survive a save is the trigger to revisit D3-2 | `corpus/cmsenc.py`, `corpus/make-signed.py` |
| **The hybrid-reference file leaves its packed objects OUT of the classic table.** ISO 32000-1 §7.5.8.4 also permits listing them there as *free* entries, and some producers do. That arrangement is not in the corpus, and `pdf/cos/XrefTable` merges free entries first-seen-wins with the classic table read before `/XRefStm` — so how this reader treats it is unmeasured, not known to work | `ordinary/hybrid-xrefstm.pdf` |
| **The embedded fonts are nine hanzi and seven capitals, all built from rectangles.** They are *real characters correctly drawn* — 一 二 三 十 口 日 田 中 目 and E F H I L T — chosen because those glyphs genuinely are straight strokes. No curves, no hinting, no kerning, no composite glyphs. What is exercised is the embedding path (`/FontFile2`, CIDFontType2, Identity-H, `/W`), not typographic fidelity | `corpus/fontenc.py` |
| **The Hebrew is thirteen APPROXIMATE letters, and the readable CJK is whatever face the machine has.** Stage 5 T1 added Hebrew to `fontenc.py` for `ordinary/rtl-text.pdf`: square Hebrew is mostly straight strokes, but its joins and serifs are not rectangles, so a reader sees block letters of the right proportions rather than the letters themselves — enough for what the file tests, right-to-left ORDER through codes, positions and `/ToUnicode`, and not a claim about Hebrew typography. `ordinary/cjk-readable.pdf` goes the other way: it embeds nothing, so the engine draws its characters with the machine's own CJK face (SimSun where it was generated), which makes them readable and makes that one file's golden baseline **machine-dependent** (`golden/manifest.json`) | `corpus/fontenc.py`; `corpus/make-text.py` |
| **Every Stage 5 claim is about THESE documents.** The text fixtures, their labels and the comparison pairs are regular, set in base-14 Helvetica or rectangle glyphs, and revised only in the ways their records list. A layout figure measured on them, or a comparison that is right on them, says nothing about real documents — which no Stage 5 case meets, by the user's decision (`Stage5-TextSearch-Plan.md` D5-23) | `corpus/make-text.py`, `corpus/make-compare.py` |

## How to regenerate

```powershell
python corpus/make-corpus.py                    # write every file
python corpus/make-corpus.py --verify           # regenerate in memory, compare against disk
python corpus/make-corpus.py --check-provenance # every file has a row here; every row has a file
python corpus/make-corpus.py --check-faults     # the Stage 5 text fixtures carry what their rows claim
python corpus/make-compare.py --self-test       # every comparison record re-derives its revised file
python corpus/make-corpus.py --list             # every file with its size and sha256
```

Individual generators (`make-ordinary.py`, `make-malformed.py`, `make-encrypted.py`, `make-signed.py`,
`make-text.py`, `make-compare.py`) can be run on their own and write only their own files — `make-text.py`
writes into `ordinary/` and `labels/`, `make-compare.py` into `compare/`. `make-minimal.py` and `make-a2-text-page.py` predate
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
| `corpus/cmsenc.py` | Detached CMS signatures, sign side (Stage 3 T1, D3-2): DER, a **deterministic** RSA-2048 key derived from a public seed, a self-signed X.509 v3 certificate, and an RFC 5652 `SignedData` with signed attributes. **No key is stored** — it is re-derived at generation time. **Test-data only**; Stage 11's signing shares no code with it, and neither does OpenSSL, which is what lets OpenSSL judge it |
| `corpus/make-signed.py` | `corpus/signed/`: serializes each file with a fixed-width `/ByteRange` and a zero-filled `/Contents`, fills the four numbers without moving a byte, then signs the two ranges. Asserts the private exponent appears in no output |
| `corpus/make-text.py` | **Stage 5 T1.** The text fixtures in `ordinary/` and, beside each, its **ground truth** in `labels/<name>.json`: every run of text it placed, in content order, with its logical text, baseline, size and horizontal extent; the runs grouped into blocks with a role, **listed in reading order**; the column count; each table's box and grid, and the regions that look tabular and are not. Horizontal extents are exact — the standard Helvetica widths, measured from Arial through Pillow once and written in as constants so no system font is read at generation time. The labels are what `Stage5-TextSearch-Testing-Guide.md` §3.2's layout measures and A-17's extraction truth are graded against: *the oracle for a text claim is the generator, not the engine* |
| `corpus/make-compare.py` | **Stage 5 T1.** `compare/`: before/after pairs built from a small document model, each revised by a direct edit of the model, with `<name>.expected.json` listing the changes as records a program can apply. `--self-test` re-derives every revised model from its base by applying the records with code of its own, requires an exact match, checks what each kind of pair promises about its words, and drives four controls (`gates.corpus.compare`, guide case A-15) |
| `corpus/fixturecheck.py` | **Stage 5 T1.** `make-corpus.py --check-faults` (`gates.corpus.faults`, guide case A-14): reads each Stage 5 fixture's **bytes**, every stream decoded — attachments that are PDFs included — and requires the fault or feature its row claims, and that the control word appears nowhere. Never through `pdf/cos/` or PDFium, which are what the fixtures exist to test |

Each encoder **self-checks against an independent implementation** before its output reaches a
corpus file: `deflate.py` round-trips through `zlib.decompress`, `ccitt.py` through its own
decoder, `cryptenc.py` against the FIPS-197 known-answer vectors, `cmsenc.py` by re-doing the RSA
public operation and — when `cryptography` is importable — verifying the certificate and the
signature with it, and — opportunistically, when the
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
| `ordinary/dests-named.pdf` | `make-ordinary.py` | **Stage 4 T1.** Named destinations in **both** forms a PDF may carry: a `/Names /Dests` **name tree** — a real two-level tree with `/Kids` and `/Limits`, since walking it is the part that goes wrong — and the PDF 1.1 flat **`/Dests` dictionary** in the catalog. Three destination kinds resolve (`/XYZ`, `/Fit`, `/FitH`), and the tree writes its first kid’s entries in the `<< /D […] >>` dictionary form and its second’s as bare arrays, both legal and both common. **The control is `ghost-page`, whose destination names object 9999, which does not exist** — not damage but a dead link, the thing a deleted page leaves behind: the file opens and renders, and a resolver must **report** that destination as unresolvable rather than drop it silently (D4-3). A fixture where every destination resolves cannot tell the two apart |
| `ordinary/cover-odd-count.pdf` | `make-ordinary.py` | Seven pages with a cover, `/PageLayout /TwoPageRight`. The spread-parity case: cover alone, then 2-3, 4-5, 6-7. **With an even page count the off-by-one is invisible**, which is why the count is odd |
| `ordinary/rtl-binding.pdf` | `make-ordinary.py` | `/ViewerPreferences /Direction /R2L`. RTL binding changes which side of a spread a page sits on and nothing else — so a per-page golden image cannot catch a left-to-right assumption and the fixture has to be the whole document |
| `ordinary/cropbox-margins.pdf` | `make-ordinary.py` | **Stage 4 T1.** **Three rectangles, 72 pt apart on every side**: `/MediaBox` `0 0 595.276 841.89`, `/CropBox` `72 72 523.276 769.89`, and ink confined to `144 160 451.276 681.89`. Until this file every corpus document’s CropBox **was** its MediaBox, which is why Stage 2’s **I-28** could sit open and untestable — Fit Visible fits the CropBox, so `Ctrl+3` was `Ctrl+0` under another name and no case could see it. **Page 2 is deliberately empty**: a well-formed page with a zero-length content stream and therefore no page objects at all, carrying page 1’s boxes, which is the other half of D4-14 — a content box cannot be computed from nothing, and a fallback that lands on the MediaBox instead of the CropBox is then a visibly different size rather than something to infer |
| `ordinary/transitions.pdf` | `make-ordinary.py` | **Stage 4 T1.** Per-page `/Trans` of three kinds (`/Wipe` with `/Di`, `/Dissolve`, `/Split` with `/Dm` and `/M`), two `/Dur` values, and `/PageMode /FullScreen` in the catalog — the three things a presentation reads that the viewer’s own settings cannot supply. `/Dur` is the **page’s** display time and `/Trans`’s `/D` is the **transition’s** duration; conflating the two is the obvious way to get this wrong, so pages 1 and 3 carry both and differ in each. **Page 4 carries neither key, and that is the control** — the page where `fullScreen/transition` and `fullScreen/autoAdvanceSec` must win. A fixture whose every page overrides cannot distinguish *the document’s transition was used* from *the preference was ignored* |
| `ordinary/objstm-xrefstream.pdf` | `make-ordinary.py` | PDF 1.5 structure: objects packed into an `/ObjStm`, a cross-reference **stream** with `/W` field widths and type-2 entries. A completely separate reader path from the classic table |
| `ordinary/incremental-3gen.pdf` | `make-ordinary.py` | Three generations appended, not rewritten — what `tst_xreftable` (A-20) walks. Gen 2 replaces page 2 and adds page 3; gen 3 replaces page 1 and the `/Info`. **The earlier bytes are still there verbatim**, which is the only way to catch a reader that rebuilds instead of following `/Prev` |
| `ordinary/pages-900.pdf` | `make-ordinary.py` | 900 pages in a balanced tree of 30 × 30, with object streams. A flat 900-entry `/Kids` array is legal and is exactly the shape that hides an O(n) page lookup behind a fast small document |
| `ordinary/rotate-inherited.pdf` | `make-ordinary.py` | **Stage 3 T1.** `/Rotate 90` on the **root `/Pages` node** only: page 1 inherits it, page 2 overrides it with an **explicit `/Rotate 0`**, and page 3 inherits it **through an intermediate `/Pages` node** with no `/Rotate` of its own. Decision D3-5 writes a rotation on the page and never on an ancestor (which would rotate the siblings), and drops a `/Rotate` equal to the inherited value — both wrong in ways only an inherited value can show. Page 2 separates *"absent"* from *"says 0"*; page 3 is what a one-level `/Parent` lookup misses |
| `ordinary/hybrid-xrefstm.pdf` | `make-ordinary.py` | **Stage 3 T1.** A **hybrid-reference file**: a classic table, then an **empty update section** (`xref 0 0`) whose trailer carries `/Prev` and `/XRefStm`, pointing at a cross-reference stream listing the fonts, the `/Pages` node and both page dictionaries, which live in an object stream and are **absent from the classic table** — reachable only through `/XRefStm`. A reader that ignores the key finds a catalog with no page tree and says nothing. D3-5 writes a classic section after this file, and the packed objects must still resolve. **Two sections since 2026-09-17** (Stage 3 guide I-14): Word writes hybrid files this way, and Acrobat reads `/XRefStm` only from a trailer that has `/Prev` — the T1 file, one section with the key in its only trailer, was valid by ISO 32000-1 §7.5.8.4, opened in PDFium, and **Acrobat reported it damaged** (measured over eleven variants in M-02's walk). The one-section form is built inline in `tests/unit/tst_incrementalwriter.cpp`, not kept here. Read the limitation above: the free-entry arrangement is not here |
| `ordinary/stream-length-control.pdf` | `make-ordinary.py` | The **control** for `malformed/length-lies-short.pdf`. A-17 asks for a lying `/Length` *and* a correct one, because the second is what proves the recovery path actually ran. The damaged twin is built by importing this generator, so the pair cannot drift apart — they differ in exactly three bytes |
| `ordinary/columns-two.pdf` | `make-text.py` | **Stage 5 T1.** Two columns under a spanning headline, a footnote, a running header and footer, the prose **flowing over two pages** — so a selection can run from the foot of one column to the head of the next and across a page break — then a **one-column control page**. Label: `labels/columns-two.json` — whose page 2 `columns` was corrected from 2 to **1** at Stage 5 T4: the flow's prose ends in that page's left column, and a page's `columns` counts the columns that hold body text (S5 guide I-03) |
| `ordinary/columns-three.pdf` | `make-text.py` | **Stage 5 T1.** Three columns under a spanning headline, then a one-column control page. Label: `labels/columns-three.json` |
| `ordinary/table-ruled.pdf` | `make-text.py` | **Stage 5 T1.** A **ruled** table (six rows, four columns), an **unruled** one aligned by position alone, and a numbered list with a hanging indent — the **negative**: it looks tabular and is not, which is what a table detector's precision is measured against. Label: `labels/table-ruled.json` |
| `ordinary/rtl-text.pdf` | `make-text.py` | **Stage 5 T1.** Four Hebrew lines, one mixed with digits. Page 1 emits them in **visual** order, as producers do; page 2 draws the same glyphs at the same places emitted in **logical** order — two pages that look the same and whose content streams disagree about order. The letters are rectangle approximations (see the limit above). Label: `labels/rtl-text.json`, each line's logical and visual strings |
| `ordinary/cjk-vertical.pdf` | `make-text.py` | **Stage 5 T1.** Vertical CJK through **Identity-V** with `/ToUnicode`, two columns read right to left, and a horizontal line of the same glyphs as the control. Label: `labels/cjk-vertical.json` |
| `ordinary/hyphenated.pdf` | `make-text.py` | **Stage 5 T1.** Line-final hyphens that **join** a word across the break (`exam-`/`ple`, `in-`/`cremental`), one that must **not** join because the next line begins with a capital (`North-`/`West`), and `well-known` **mid-line** — a real compound, the control. Label: `labels/hyphenated.json` |
| `ordinary/text-tounicode.pdf` | `make-text.py` | **Stage 5 T1.** `/ToUnicode` on two kinds of font. Page 1: base-14 Helvetica whose `/Differences` put the `fi` ligature glyph at **two codes — one mapped to U+FB01, one to `f` `i`** — an `e` with the spacing acute kerned over it and mapped to **U+0301** (a decomposed é) beside WinAnsi's precomposed one, and the embedded Latin subset with its own `/ToUnicode`. Page 2: `cjk-embedded.pdf`'s synthetic subset and Identity-H **with** a `/ToUnicode` — the twin of that file, which stays the **unmapped control**. Label: `labels/text-tounicode.json` |
| `ordinary/cjk-readable.pdf` | `make-text.py` | **Stage 5 T1.** CJK a person can read: a **non-embedded** Adobe-GB1 font (`/STSong-Light`) through the predefined **`/UniGB-UCS2-H`** and **`/UniGB-UCS2-V`** CMaps, drawn with the machine's own face — horizontal lines and two vertical columns. **A file of its own rather than a third page of `text-tounicode.pdf`**, because the capture records a substituted face per document and a shared file made that one's Latin baseline machine-dependent (measured at T1). The one Stage 5 golden case marked machine-dependent. Label: `labels/cjk-readable.json` |
| `ordinary/search-classes.pdf` | `make-text.py` | **Stage 5 T1.** One unique word in **each** content class of `Features.md` §1.8: `papaya` in the page text, `quokka` in a note's `/Contents`, `zeppelin` as a bookmark title, `marmot` and `walrus` as a field's name and value, and among the attachments `ocelot.txt` with the description `narwhal` and `lemur.pdf`, whose page says `axolotl`. Must be found **nowhere**: `wombat` (the control, in no byte of the file), `hedgehog` (the *content* of the `.txt` — a non-PDF attachment is searched by name and description only) and `pangolin` (a PDF attached **inside** `lemur.pdf` — one level deep only). Label: `labels/search-classes.json` |
| `ordinary/text-900.pdf` | `make-text.py` | **Stage 5 T1.** 900 **dense** pages — `a2-text-page.pdf`'s density — in a 30 × 30 tree: page N's text is a marker line naming N, then one of ten shared bodies. The workload A2 at Stage 5 is measured on (plan D5-22) and the one *"the second search of a 900-page file is instant"* is defined over (guide §3.3); `pages-900.pdf`'s text is one line a page and could be neither. Label: `labels/text-900.json`, the ten bodies and the marker's form, from which every page's text follows |

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

**Not here, and why.** No public-key (`/Filter /Adobe.PubSec`) encryption: opening it needs a
recipient's private key, and a private key committed to a repository is a bad habit even when it
protects nothing. Signed documents are not here either — they are `signed/`, below, from Stage 3 T1;
signing *in the product* is Stage 11. (Until T1 this paragraph said *"signing is Stage 10"*, which
`Implementation-Plan.md` never did.)

## `signed/` — genuinely signed, by an identity that is not anyone

**Stage 3 T1, decision D3-2** (`claude-plan/Stage3-Mutation-Plan.md` §3). These exist for **risk A4**
(`Tech-Stack-Plan.md` §3.3): *does an incremental save leave a pre-existing signature valid?* Stage 3
T2 writes a rotation into each with the incremental writer alone and T13 through the product's
Save, and both times the result is judged by **OpenSSL** (`openssl cms -verify`, the integrity of
the signed range) and by **Acrobat** (modification policy).

**Every signature is real and means nothing.** The key is RSA-2048, **derived at generation time from
a public seed** in `cmsenc.py` and never written to disk; the certificate is self-signed and its
subject reads *"RawPage corpus test identity - NOT a real signer"*. Each signature is invisible (an
empty `/Rect`) and `/SubFilter /adbe.pkcs7.detached`. **Read the signature limitation at the top of
this file before quoting what these prove.**

**The validator is shown a wrong answer first.** `tests/gates/check-signatures.py` reads each file as
bytes — never through `pdf/cos/` — flips one byte in each signed range, and requires OpenSSL to
**reject** both copies before it lets the unmodified file's verification count
(`gates.corpus.signed`, guide case A-15). `--shapes` checks the structure each row below claims
(A-16).

| File | Signature | What it is for |
|---|---|---|
| `signed/approval.pdf` | Approval, classic table | The plain case: one revision, one signature covering the whole file |
| `signed/approval-xrefstream.pdf` | Approval, xref stream | The page and its furniture are packed in an **object stream**, so the page dictionary a rotation changes is a type-2 object that D3-5 must write out as an ordinary one. The **signature dictionary is kept out** of the object stream, because `/ByteRange` needs its byte offsets in the file |
| `signed/certified-nochanges.pdf` | **Certification**, `/DocMDP /P 1` | **The control.** *No changes permitted*: Stage 3 saves a rotation into it without honouring that (Stage 11's job), so a policy validator **must** flag the result — or its verdict on the three approval files was not a judgement |
| `signed/approval-then-update.pdf` | Approval, then an **unsigned incremental update** | The update replaces `/Info` and keeps `/ID`'s first element. The signed range ends at the **first** `%%EOF`, and a save appends a **third** revision — the file that catches a writer measuring *"the original bytes"* from the newest revision, or a validator assuming a signature covers the whole file |

## `compare/` — before/after pairs, every difference written down

**Stage 5 T1** (`claude-plan/Stage5-TextSearch-Plan.md` D5-17), by `make-compare.py`. Each pair is a base
and a revision of it, with `<name>.expected.json` holding the changes as records — kind, page, place,
words before and after — plus a one-line summary each for a person. **Every record is checked**: the
generator's `--self-test` re-derives the revised model from the base by applying the records with code
of its own and requires an exact match, so a change the list forgets cannot pass (guide case A-15).
The first eleven pairs share `compare/base.pdf`: three pages, one column, a running header, a dated
footer with page numbers, and one image.

| File | Pair | What it is for |
|---|---|---|
| `compare/base.pdf` | — | The base the next eleven revisions are made from |
| `compare/word-inserted.pdf` | with `base.pdf` | One word inserted in a paragraph, which then re-wraps |
| `compare/word-deleted.pdf` | with `base.pdf` | One word deleted |
| `compare/word-replaced.pdf` | with `base.pdf` | One word replaced by another |
| `compare/paragraph-moved.pdf` | with `base.pdf` | A whole paragraph moved to the top of its page — a **move**, not a deletion and an insertion |
| `compare/image-moved.pdf` | with `base.pdf` | The image moved 180 pt; **no text changed**, so only an appearance comparison can see it |
| `compare/page-inserted.pdf` | with `base.pdf` | A page inserted as page 2; later pages renumber with it |
| `compare/page-deleted.pdf` | with `base.pdf` | Page 2 deleted |
| `compare/pages-reordered.pdf` | with `base.pdf` | Pages 2 and 3 swapped |
| `compare/header-changed.pdf` | with `base.pdf` | The running header changed on every page — what the header/footer ignore-list silences |
| `compare/page-numbers-shifted.pdf` | with `base.pdf` | Every page number 4 higher and nothing else — what the page-number ignore-list silences |
| `compare/whitespace-only.pdf` | with `base.pdf` | Three single spaces made double; no word changed — what the whitespace ignore-list silences |
| `compare/identical.pdf` | with `base.pdf` | **The control**: identical pages, only the Info `/Title` differs. A comparison must report nothing |
| `compare/unrelated-a.pdf` | with `unrelated-b.pdf` | 50 pages drawn from one vocabulary… |
| `compare/unrelated-b.pdf` | with `unrelated-a.pdf` | …and 50 from another that **shares no word** with it: the bound on comparing documents with nothing in common (guide §3.4) |
| `compare/report-v1.pdf` | with `report-v2.pdf` | **The walkable pair** (plan D5-23): a four-page, two-column report with a title, a figure and a ruled table… |
| `compare/report-v2.pdf` | with `report-v1.pdf` | …revised the way a person revises one, several edits at once: two words replaced, a sentence added, a paragraph moved, the figure moved, a table cell changed, a page inserted, and every footer's date changed — `compare/report.expected.json` lists all eight, and guide case M-08 walks it |
