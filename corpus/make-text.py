#!/usr/bin/env python3
"""Generates Stage 5's text fixtures in `corpus/ordinary/`, and the GROUND TRUTH for each in
`corpus/labels/`.

WHY A GENERATOR OF ITS OWN. `make-ordinary.py` is two thousand lines of files that are supposed to
open and render. These nine are supposed to be READ -- extracted, selected, searched, laid out -- and
each one comes with a second output that nothing before Stage 5 needed: a label file saying what
text the generator placed, where, in which order, and with which meaning. `Stage5-TextSearch-Plan.md`
section 1 is why: *the oracle for a text claim is the generator, not the engine*. A test that
compares the extractor with PDFium's own `FPDFText_GetText` proves PDFium agrees with itself; a test
that compares it with what this file wrote proves the product reads what is there.

WHAT A LABEL FILE HOLDS, per page (`labels/<name>.json`):

  * `lines`  -- every run of text the generator placed, in CONTENT order, with its logical text,
    its baseline, its size, its horizontal extent and a box. A "line" is one run: a table cell is
    its own line, a vertical column of CJK is one line with `"direction": "ttb"`.
  * `blocks` -- lines grouped the way a reader groups them (a paragraph fragment inside one column,
    a table, a header), each with a `role`, LISTED IN READING ORDER. `order` repeats the block ids.
  * `columns` -- how many body columns the page has.
  * `tables` -- each table's box, rows and columns; `negatives` -- regions that look tabular and are
    not, which is what a table detector's precision is measured against.

The measures in `Stage5-TextSearch-Testing-Guide.md` section 3.2 are defined over these fields.
Coordinates are PDF points in page space. Horizontal extents are EXACT: they come from the
standard Helvetica widths below, which are what PDFium's own metric-compatible base-14 faces
advance by. Vertical extents are the font's ascent and descent, and are a box, not ink.

WHAT THIS DOES NOT CLAIM. Every file here is ours (D2-11), regular, and set in base-14 Helvetica or
in `fontenc.py`'s rectangle glyphs. A layout figure measured on these files is a figure about
documents we generated -- never about real ones, which `Stage5-TextSearch-Plan.md` D5-23 says no
Stage 5 case measures.

    python corpus/make-text.py              # write the fixtures and their labels
    python corpus/make-corpus.py --verify   # prove they regenerate byte-identically
"""

import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import fontenc                                                        # noqa: E402
import pdfbuild as pb                                                 # noqa: E402

FILES = {}


def generator(name):
    def register(function):
        FILES[name] = function
        return function
    return register


# ---------------------------------------------------------------------------------------------
# Helvetica metrics
# ---------------------------------------------------------------------------------------------
# Advance widths per 1000 em for codes 32..126 -- the standard Helvetica and Helvetica-Bold AFM
# values. Taken 2026-09-29 by measuring Arial and Arial Bold, which are drawn metric-compatible
# with Helvetica, through Pillow, and checked against the AFM values that are common knowledge
# (space 278, i 222, m 833, W 944, quotesingle 191, asciitilde 584). WRITTEN HERE AS CONSTANTS so
# that generation never reads a font off the machine it runs on: the Ubuntu runner has no Arial,
# and a generator whose output depended on it would stop regenerating byte-identically there.
HELVETICA = [
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556,
    556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667,
    611, 778, 722, 278, 500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667,
    667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500,
    222, 833, 556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584]
HELVETICA_BOLD = [
    278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556,
    556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611, 975, 722, 722, 722, 722, 667,
    611, 778, 722, 278, 556, 722, 611, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667,
    667, 611, 333, 278, 333, 584, 556, 333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556,
    278, 889, 611, 611, 611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584]

# Helvetica's ascender and descender, per em. A label box is [x0, baseline - DESCENT * size,
# x1, baseline + ASCENT * size].
ASCENT = 0.718
DESCENT = 0.207


def width(text, size, bold=False):
    table = HELVETICA_BOLD if bold else HELVETICA
    total = 0
    for character in text:
        code = ord(character)
        if not 32 <= code <= 126:
            raise ValueError("width() is for ASCII only, not %r" % character)
        total += table[code - 32]
    return total * size / 1000.0


def wrap(words, size, limit, bold=False):
    """Greedy line breaking by measured width -- the lines a label records are the lines drawn."""
    lines, current = [], []
    for word in words:
        trial = " ".join(current + [word])
        if current and width(trial, size, bold) > limit:
            lines.append(" ".join(current))
            current = [word]
        else:
            current.append(word)
    if current:
        lines.append(" ".join(current))
    return lines


def r3(value):
    return round(value, 3)


# ---------------------------------------------------------------------------------------------
# One page: its operators, and what the generator knows about it
# ---------------------------------------------------------------------------------------------

class Page:
    """Collects a page's content operators and its ground truth side by side, so that nothing can
    be drawn without being recorded and nothing recorded that was not drawn."""

    def __init__(self, media=pb.A4):
        self.media = media
        self.ops = []
        self.lines = []
        self.blocks = []
        self.tables = []
        self.negatives = []
        self.columns = 1
        self.notes = {}

    def op(self, data):
        self.ops.append(data)

    def record(self, text, x0, x1, baseline, size, box=None, **extra):
        entry = {"id": len(self.lines), "text": text, "x0": r3(x0), "x1": r3(x1),
                 "baseline": r3(baseline), "size": r3(size),
                 "box": box or [r3(x0), r3(baseline - DESCENT * size), r3(x1),
                                r3(baseline + ASCENT * size)]}
        entry.update(extra)
        self.lines.append(entry)
        return entry["id"]

    def text(self, x, y, size, text, bold=False):
        """One Helvetica run, drawn and recorded."""
        self.op(pb.show_text(x, y, size, text, b"/F2" if bold else b"/F1"))
        return self.record(text, x, x + width(text, size, bold), y, size)

    def block(self, role, line_ids):
        self.blocks.append({"id": len(self.blocks), "role": role, "lines": list(line_ids)})
        return self.blocks[-1]["id"]

    def content(self):
        return b"\n".join(self.ops) + b"\n"

    def label(self, index):
        return {"index": index, "columns": self.columns, "lines": self.lines,
                "blocks": self.blocks, "order": [block["id"] for block in self.blocks],
                "tables": self.tables, "negatives": self.negatives, **self.notes}


def helvetica_resources(pdf, extra_fonts=()):
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    fonts = [(b"/F1", pb.ref(regular)), (b"/F2", pb.ref(bold))] + list(extra_fonts)
    return pb.dictionary([(b"/Font", pb.dictionary(fonts))])


def info(pdf, title, author, subject, date):
    """The Info dictionary, with an AUTHOR and a DATE of the file's own. Stage 5's Advanced Search
    filters by author, subject and date (plan D5-13), and its walk over a copy of the corpus tree
    (D5-23) needs files that differ in all three -- every earlier corpus file shares one fixed
    instant and names no author."""
    return pdf.add(pb.dictionary([
        (b"/Title", pb.utf16(title)), (b"/Author", pb.utf16(author)),
        (b"/Subject", pb.utf16(subject)), (b"/Producer", pb.literal(pb.PRODUCER)),
        (b"/CreationDate", pb.literal(date)), (b"/ModDate", pb.literal(date))]))


def document(pdf, pages, resources, extra_pairs=None, root_extra=()):
    """Catalog and page tree over `Page`s; returns (catalog, page object numbers). `resources` is
    one dictionary for every page, or a list with one per page -- which matters when a page names
    a font the engine resolves off the machine: shared resources make every page report that
    substitution, and a golden baseline of a page that never draws the font would be recorded as
    machine-dependent for nothing (measured at Stage 5 T1 on `text-tounicode.pdf`)."""
    pages_number = pdf.reserve()
    numbers = []
    for index, page in enumerate(pages):
        contents = pb.content_stream(pdf, page.content())
        extra = extra_pairs(index) if extra_pairs else ()
        own = resources[index] if isinstance(resources, list) else resources
        numbers.append(pdf.add(pb.page(pages_number, page.media, contents, own, extra)))
    pdf.set(pages_number, pb.dictionary([
        (b"/Type", b"/Pages"), (b"/Kids", pb.array([pb.ref(n) for n in numbers])),
        (b"/Count", pb.num(len(numbers)))]))
    catalog = pdf.add(pb.dictionary(
        [(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number))] + list(root_extra)))
    return catalog, numbers


def label_bytes(name, pages, **top):
    body = {"file": "ordinary/" + name, "generator": "corpus/make-text.py",
            "units": "PDF points, page space", **top,
            "pages": [page.label(index) if isinstance(page, Page) else page
                      for index, page in enumerate(pages)]}
    return (json.dumps(body, ensure_ascii=False, indent=1) + "\n").encode("utf-8")


# ---------------------------------------------------------------------------------------------
# /ToUnicode and the embedded composite font
# ---------------------------------------------------------------------------------------------

def tounicode(pdf, entries, code_bytes):
    """A /ToUnicode CMap stream. `entries` is [(code, text)], `code_bytes` 1 or 2.

    bfchar only, in chunks of 100 (the CMap limit per section). `text` may be several characters --
    which is how a ligature glyph says it means two letters, and exactly what `text-tounicode.pdf`
    uses to map one `fi` glyph to U+FB01 and another to "fi"."""
    digits = code_bytes * 2
    low = b"0" * digits
    high = b"F" * digits
    out = [b"/CIDInit /ProcSet findresource begin", b"12 dict begin", b"begincmap",
           b"/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def",
           b"/CMapName /RawPage-Corpus-UCS def", b"/CMapType 2 def",
           b"1 begincodespacerange", b"<" + low + b"> <" + high + b">", b"endcodespacerange"]
    entries = sorted(entries)
    for start in range(0, len(entries), 100):
        chunk = entries[start:start + 100]
        out.append(b"%d beginbfchar" % len(chunk))
        for code, text in chunk:
            out.append(b"<%0*X> <%s>" % (digits, code, text.encode("utf-16-be").hex().upper().encode()))
        out.append(b"endbfchar")
    out += [b"endcmap", b"CMapName currentdict /CMap defineresource pop", b"end", b"end"]
    data, filter_name = pb.flate(b"\n".join(out) + b"\n")
    return pdf.add_stream([(b"/Filter", filter_name)], data)


def embedded_composite(pdf, maker, ps_name, encoding, with_tounicode=True):
    """A Type0 / CIDFontType2 font over one of `fontenc.py`'s synthetic faces, Identity-H or
    Identity-V: the string bytes ARE glyph ids. Returns (font object, {character: glyph id},
    {glyph id: advance})."""
    font_bytes, mapping, widths = maker()
    font_data, font_filter = pb.flate(font_bytes)
    font_file = pdf.add_stream([(b"/Length1", pb.num(len(font_bytes))),
                                (b"/Filter", font_filter)], font_data)
    descriptor = pdf.add(pb.dictionary([
        (b"/Type", b"/FontDescriptor"), (b"/FontName", pb.name(ps_name)),
        (b"/Flags", b"4"), (b"/FontBBox", pb.rect(0, -220, 1000, 880)),
        (b"/ItalicAngle", b"0"), (b"/Ascent", b"880"), (b"/Descent", b"-120"),
        (b"/CapHeight", b"700"), (b"/StemV", b"80"), (b"/FontFile2", pb.ref(font_file))]))
    width_array = pb.array([b"1", pb.array([pb.num(widths[index])
                                            for index in range(1, len(widths))])])
    cid_font = pdf.add(pb.dictionary([
        (b"/Type", b"/Font"), (b"/Subtype", b"/CIDFontType2"), (b"/BaseFont", pb.name(ps_name)),
        (b"/CIDSystemInfo", pb.dictionary([(b"/Registry", pb.literal(b"Adobe")),
                                           (b"/Ordering", pb.literal(b"Identity")),
                                           (b"/Supplement", b"0")])),
        (b"/FontDescriptor", pb.ref(descriptor)), (b"/DW", b"1000"), (b"/W", width_array),
        (b"/CIDToGIDMap", b"/Identity")]))
    pairs = [(b"/Type", b"/Font"), (b"/Subtype", b"/Type0"), (b"/BaseFont", pb.name(ps_name)),
             (b"/Encoding", encoding), (b"/DescendantFonts", pb.array([pb.ref(cid_font)]))]
    by_char = {chr(code): gid for code, gid in mapping.items()}
    if with_tounicode:
        pairs.append((b"/ToUnicode", pb.ref(tounicode(
            pdf, [(gid, character) for character, gid in by_char.items()], 2))))
    return pdf.add(pb.dictionary(pairs)), by_char, {gid: widths[gid] for gid in range(len(widths))}


def glyph_run(font_key, size, x, y, gids):
    codes = b"".join(b"%04X" % gid for gid in gids)
    return b"BT %s %s Tf 1 0 0 1 %s %s Tm <%s> Tj ET" % (
        font_key, pb.num(size), pb.num(x), pb.num(y), codes)


# ---------------------------------------------------------------------------------------------
# Multi-column flow
# ---------------------------------------------------------------------------------------------

BODY_SIZE = 9.5
BODY_LEADING = 12.0
PARAGRAPH_GAP = 6.0


def paragraphs(seed, count, words=(38, 71)):
    """Deterministic prose, `count` paragraphs of varying length from pdfbuild's word walk."""
    low, span = words
    out = []
    for index in range(count):
        length = low + (seed * 7 + index * 13) % (span - low)
        text = pb.words_for(seed * 100 + index, length)
        text[0] = text[0].capitalize()
        text[-1] += "."
        out.append(text)
    return out


def flow(frames, paras):
    """Pour paragraphs into (page, x, top, bottom, width) frames in order. A paragraph that
    crosses a frame edge becomes two blocks, one per frame -- which is how a reader sees it, and
    what a reading-order label has to say. Greedy wrapping restarted at the break gives the same
    lines as wrapping the whole paragraph, because every frame in one flow has one width. Returns
    the paragraphs that did not fit, which every caller asserts is none."""
    queue = [list(paragraph) for paragraph in paras]
    for page, x, top, bottom, frame_width in frames:
        y = top
        while queue:
            room = int((y - bottom) // BODY_LEADING) + 1 if y >= bottom else 0
            if room <= 0:
                break
            lines = wrap(queue[0], BODY_SIZE, frame_width)[:room]
            ids = []
            for line in lines:
                ids.append(page.text(x, y, BODY_SIZE, line))
                y -= BODY_LEADING
            page.block("body", ids)
            used = sum(len(line.split(" ")) for line in lines)
            if used >= len(queue[0]):
                queue.pop(0)
                y -= PARAGRAPH_GAP
            else:
                queue[0] = queue[0][used:]
                break
        if not queue:
            break
    return queue


def furniture(page, name, number, total):
    header = page.text(54, 812, 7.5, "RawPage corpus - " + name)
    page.block("header", [header])
    return page.text(54, 30, 7.5, "page %d of %d" % (number, total))


# ---------------------------------------------------------------------------------------------
# The fixtures
# ---------------------------------------------------------------------------------------------

@generator("columns-two.pdf")
def columns_two():
    """Two columns under a spanning headline, a footnote, a running header and footer, flowing
    over two pages -- so a selection can run from the foot of one column to the head of the next,
    and across the page break -- and a ONE-COLUMN control page after them."""
    pdf = pb.Pdf()
    resources = helvetica_resources(pdf)
    pages = [Page(), Page(), Page()]
    name = "columns-two.pdf"
    footers = [furniture(page, name, index + 1, 3) for index, page in enumerate(pages)]

    first, second, control = pages
    headline = first.text(54, 772, 18, "Two columns and a spanning headline", bold=True)
    first.block("headline", [headline])
    left, right, gutter = 54.0, 541.276, 24.0
    column = (right - left - gutter) / 2
    frames = [(first, left, 744, 104, column), (first, left + column + gutter, 744, 104, column),
              (second, left, 790, 60, column), (second, left + column + gutter, 790, 60, column)]
    rest = flow(frames, paragraphs(21, 14))
    assert not rest, "columns-two: the prose did not fit its frames"
    for page in (first, second):
        page.columns = 2

    first.op(b"0 G 0.5 w 54 94 m 204 94 l S")
    note = first.text(54, 84, 7, "1 A footnote: it belongs to the page, and is read after the body.")
    first.block("footnote", [note])

    heading = control.text(54, 772, 14, "One column: the control page", bold=True)
    control.block("headline", [heading])
    rest = flow([(control, left, 748, 60, right - left)], paragraphs(22, 5))
    assert not rest, "columns-two: the control page overflowed"

    for page, footer in zip(pages, footers):
        page.block("footer", [footer])
    catalog, _ = document(pdf, pages, resources)
    data = pdf.serialize(catalog, info(pdf, "Two columns", "Ines Ferreira",
                                       "Layout: two columns and a control page",
                                       b"D:20260911090000Z"))
    return data, label_bytes(name, pages)


@generator("columns-three.pdf")
def columns_three():
    """Three columns under a spanning headline, then a one-column control page."""
    pdf = pb.Pdf()
    resources = helvetica_resources(pdf)
    pages = [Page(), Page()]
    name = "columns-three.pdf"
    footers = [furniture(page, name, index + 1, 2) for index, page in enumerate(pages)]
    first, control = pages

    headline = first.text(54, 772, 18, "Three columns, one headline", bold=True)
    first.block("headline", [headline])
    left, right, gutter = 54.0, 541.276, 18.0
    column = (right - left - 2 * gutter) / 3
    frames = [(first, left + index * (column + gutter), 744, 60, column) for index in range(3)]
    rest = flow(frames, paragraphs(31, 12, (30, 52)))
    assert not rest, "columns-three: the prose did not fit"
    first.columns = 3

    heading = control.text(54, 772, 14, "One column: the control page", bold=True)
    control.block("headline", [heading])
    rest = flow([(control, left, 748, 60, right - left)], paragraphs(32, 5))
    assert not rest, "columns-three: the control page overflowed"

    for page, footer in zip(pages, footers):
        page.block("footer", [footer])
    catalog, _ = document(pdf, pages, resources)
    data = pdf.serialize(catalog, info(pdf, "Three columns", "Tomasz Wolny",
                                       "Layout: three columns and a control page",
                                       b"D:20260912103000Z"))
    return data, label_bytes(name, pages)


RULED_TABLE = [
    ("Component", "Stage", "Cases", "State"),
    ("PageTextModel", "5", "12", "planned"),
    ("LayoutAnalyzer", "5", "18", "planned"),
    ("SearchEngine", "5", "24", "planned"),
    ("DocumentIndex", "5", "9", "planned"),
    ("DocumentComparer", "5", "15", "planned"),
]
ALIGNED_TABLE = [
    ("Setting", "Default", "Page"),
    ("wholeWords", "off", "Search"),
    ("caseSensitive", "off", "Search"),
    ("maxResults", "500", "Search"),
    ("snapshotDpi", "0", "Page Display"),
]


@generator("table-ruled.pdf")
def table_ruled():
    """A ruled table, an unruled aligned one, and a numbered list with a hanging indent that LOOKS
    tabular and is not -- the negative a table detector's precision is measured against."""
    pdf = pb.Pdf()
    resources = helvetica_resources(pdf)
    page = Page()
    name = "table-ruled.pdf"
    footer = furniture(page, name, 1, 1)

    title = page.text(54, 780, 14, "Two tables and a list that is not one", bold=True)
    page.block("headline", [title])
    intro = wrap(pb.words_for(4101, 30), BODY_SIZE, 487)
    page.block("body", [page.text(54, 756 - i * BODY_LEADING, BODY_SIZE, line)
                        for i, line in enumerate(intro)])

    # The ruled table: every cell its own line, the grid drawn as rules.
    widths = [130, 60, 60, 90]
    row_height = 18.0
    x0, top = 54.0, 712.0
    xs = [x0]
    for w in widths:
        xs.append(xs[-1] + w)
    bottom = top - row_height * len(RULED_TABLE)
    rules = [b"0 G 0.6 w"]
    for row in range(len(RULED_TABLE) + 1):
        y = top - row * row_height
        rules.append(b"%s %s m %s %s l S" % (pb.num(xs[0]), pb.num(y), pb.num(xs[-1]), pb.num(y)))
    for x in xs:
        rules.append(b"%s %s m %s %s l S" % (pb.num(x), pb.num(top), pb.num(x), pb.num(bottom)))
    page.op(b" ".join(rules))
    cells = []
    for row, values in enumerate(RULED_TABLE):
        baseline = top - (row + 1) * row_height + 5.5
        for col, value in enumerate(values):
            cells.append(page.text(xs[col] + 4, baseline, 9, value, bold=(row == 0)))
    page.block("table", cells)
    page.tables.append({"box": [r3(xs[0]), r3(bottom), r3(xs[-1]), r3(top)],
                        "rows": len(RULED_TABLE), "cols": len(widths), "ruled": True})

    middle = wrap(pb.words_for(4102, 26), BODY_SIZE, 487)
    page.block("body", [page.text(54, 580 - i * BODY_LEADING, BODY_SIZE, line)
                        for i, line in enumerate(middle)])

    # The unruled table: alignment is the only evidence.
    columns_x = [54.0, 170.0, 250.0]
    top2 = 530.0
    cells = []
    extents = []
    for row, values in enumerate(ALIGNED_TABLE):
        baseline = top2 - row * 15.0
        for col, value in enumerate(values):
            bold = row == 0
            cells.append(page.text(columns_x[col], baseline, 9, value, bold=bold))
            extents.append(columns_x[col] + width(value, 9, bold))
    page.block("table", cells)
    last_baseline = top2 - (len(ALIGNED_TABLE) - 1) * 15.0
    page.tables.append({"box": [r3(columns_x[0]), r3(last_baseline - DESCENT * 9), r3(max(extents)),
                                r3(top2 + ASCENT * 9)],
                        "rows": len(ALIGNED_TABLE), "cols": len(columns_x), "ruled": False})

    # The negative: numbered items with a hanging indent.
    y = 430.0
    list_lines = []
    for item in range(3):
        words = pb.words_for(4200 + item, 24)
        lines = wrap(words, BODY_SIZE, 400)
        first = page.text(54, y, BODY_SIZE, "%d." % (item + 1))
        ids = [first]
        for line in lines:
            ids.append(page.text(72, y, BODY_SIZE, line))
            y -= BODY_LEADING
        page.block("list-item", ids)
        list_lines.extend(ids)
        y -= 4
    xs_list = [line["x1"] for line in page.lines if line["id"] in list_lines]
    page.negatives.append({"role": "list", "box": [54, r3(y + BODY_LEADING - DESCENT * BODY_SIZE),
                                                   r3(max(xs_list)), r3(430 + ASCENT * BODY_SIZE)]})

    page.block("footer", [footer])
    catalog, _ = document(pdf, [page], resources)
    data = pdf.serialize(catalog, info(pdf, "Tables", "Ines Ferreira",
                                       "Layout: a ruled table, an unruled table, a list",
                                       b"D:20260913141500Z"))
    return data, label_bytes(name, [page])


# Hebrew: logical order is what a reader types; visual order is what a producer usually emits.
HEBREW_LINES = [
    "שלום בית",
    "חדר הר נהר",
    "דבר לב שם",
    "בית 123 שלום",
]


def _visual(logical):
    """A deliberately small bidi: base direction right-to-left, digits the only left-to-right
    runs, everything else right-to-left. The run order is reversed and every right-to-left run is
    reversed within itself. Enough for these four lines and no more -- the label records the result
    and the logical string both, so nothing downstream has to trust this function."""
    runs = []
    for character in logical:
        kind = "ltr" if character.isdigit() else "rtl"
        if runs and runs[-1][0] == kind:
            runs[-1][1].append(character)
        else:
            runs.append((kind, [character]))
    out = []
    for kind, characters in reversed(runs):
        out.extend(reversed(characters) if kind == "rtl" else characters)
    return "".join(out)


@generator("rtl-text.pdf")
def rtl_text():
    """Hebrew in VISUAL order, as producers emit it, and on page 2 the same lines emitted in
    LOGICAL order with each glyph positioned by hand -- two pages that look the same and whose
    content streams disagree about order, which is exactly the question Stage 5 T2 measures."""
    pdf = pb.Pdf()
    hebrew, gids, advances = embedded_composite(pdf, fontenc.hebrew_font, "RawPageCorpusHebrew",
                                                b"/Identity-H")
    resources = helvetica_resources(pdf, [(b"/F3", pb.ref(hebrew))])
    size = 20.0
    right = 520.0
    name = "rtl-text.pdf"

    def glyph_width(character):
        if character.isdigit():
            return width(character, size)
        return advances[gids[character]] * size / 1000.0

    pages = []
    for mode in ("visual", "logical"):
        page = Page()
        footer = furniture(page, name, len(pages) + 1, 2)
        caption = page.text(54, 780, 11, "rtl-text.pdf - Hebrew emitted in %s order" % mode,
                            bold=True)
        page.block("headline", [caption])
        ids = []
        for index, logical in enumerate(HEBREW_LINES):
            y = 720 - index * 40
            visual = _visual(logical)
            total = sum(glyph_width(c) for c in visual)
            x = right - total
            positions = []
            for character in visual:
                positions.append((x, character))
                x += glyph_width(character)
            if mode == "visual":
                # contiguous runs, left to right, one per font -- what a producer writes
                runs = []
                for x_at, character in positions:
                    digit = character.isdigit()
                    if runs and runs[-1][0] == digit:
                        runs[-1][2].append(character)
                    else:
                        runs.append((digit, x_at, [character]))
                for digit, x_at, characters in runs:
                    if digit:
                        page.op(pb.show_text(x_at, y, size, "".join(characters), b"/F1"))
                    else:
                        page.op(glyph_run(b"/F3", size, x_at, y, [gids[c] for c in characters]))
            else:
                # the same glyphs at the same places, one at a time, in LOGICAL order
                for slot in _logical_order(logical, visual):
                    x_at, character = positions[slot]
                    if character.isdigit():
                        page.op(pb.show_text(x_at, y, size, character, b"/F1"))
                    else:
                        page.op(glyph_run(b"/F3", size, x_at, y, [gids[character]]))
            ids.append(page.record(logical, right - total, right, y, size, direction="rtl",
                                   visual=visual, emitted=mode))
        page.block("body", ids)
        page.block("footer", [footer])
        pages.append(page)

    catalog, _ = document(pdf, pages, resources)
    data = pdf.serialize(catalog, info(pdf, "Hebrew, visual and logical", "Noa Levi",
                                       "Right-to-left text order", b"D:20260914081500Z"))
    return data, label_bytes(name, pages)


def _logical_order(logical, visual):
    """For each logical character, the index of the visual slot it occupies. Built from _visual's
    own run structure, so the logical page draws every glyph exactly where the visual page does."""
    runs = []
    for position, character in enumerate(logical):
        kind = "ltr" if character.isdigit() else "rtl"
        if runs and runs[-1][0] == kind:
            runs[-1][1].append(position)
        else:
            runs.append((kind, [position]))
    visual_positions = []
    for kind, positions in reversed(runs):
        visual_positions.extend(reversed(positions) if kind == "rtl" else positions)
    slot_of = {logical_index: slot for slot, logical_index in enumerate(visual_positions)}
    assert "".join(logical[i] for i in visual_positions) == visual
    return [slot_of[i] for i in range(len(logical))]


CJK_COLUMNS = ["一二三十口日田中目", "中日田口十三二一目"]


@generator("cjk-vertical.pdf")
def cjk_vertical():
    """Vertical CJK through Identity-V, two columns read right to left, with /ToUnicode -- and a
    horizontal line of the same glyphs on the same page as the control."""
    pdf = pb.Pdf()
    vertical, gids, _ = embedded_composite(pdf, fontenc.cjk_font, "RawPageCorpusCJK",
                                           b"/Identity-V")
    horizontal, _, _ = embedded_composite(pdf, fontenc.cjk_font, "RawPageCorpusCJK",
                                          b"/Identity-H")
    resources = helvetica_resources(pdf, [(b"/F3", pb.ref(vertical)), (b"/F4", pb.ref(horizontal))])
    page = Page()
    name = "cjk-vertical.pdf"
    footer = furniture(page, name, 1, 1)
    caption = page.text(54, 780, 11, "cjk-vertical.pdf - Identity-V, two columns right to left",
                        bold=True)
    page.block("headline", [caption])

    size = 28.0
    top = 740.0
    ids = []
    for index, text in enumerate(CJK_COLUMNS):
        x = 420.0 - index * 60.0
        page.op(glyph_run(b"/F3", size, x, top, [gids[c] for c in text]))
        ids.append(page.record(text, x - size / 2, x + size / 2, top, size, direction="ttb",
                               box=[r3(x - size / 2), r3(top - len(text) * size),
                                    r3(x + size / 2), r3(top)]))
    page.block("body", ids)
    page.columns = 2

    control_text = "日中田口"
    page.op(glyph_run(b"/F4", 24, 54, 200, [gids[c] for c in control_text]))
    control = page.record(control_text, 54, 54 + 24 * len(control_text), 200, 24,
                          direction="ltr")
    page.block("body", [control])
    page.block("footer", [footer])

    catalog, _ = document(pdf, [page], resources)
    data = pdf.serialize(catalog, info(pdf, "Vertical CJK", "Chen Wei", "Vertical writing",
                                       b"D:20260915111500Z"))
    return data, label_bytes(name, [page])


HYPHENATED = [
    "The renderer draws every page of the exam-",
    "ple document before the viewport asks for it,",
    "and a well-known trick keeps the cache warm",
    "while the worker thread finishes the in-",
    "cremental work that the scheduler queued for the",
    "tiles along the North-",
    "West edge of the page.",
]


@generator("hyphenated.pdf")
def hyphenated():
    """Line-final hyphens that JOIN a word across the break (exam-/ple, in-/cremental), one that
    must not join because the next line begins with a capital (North-/West), and a hyphen in the
    middle of a line (well-known) that is a real compound and must never be joined."""
    pdf = pb.Pdf()
    resources = helvetica_resources(pdf)
    page = Page()
    name = "hyphenated.pdf"
    footer = furniture(page, name, 1, 1)
    caption = page.text(54, 780, 11, "hyphenated.pdf - soft hyphens and a real compound", bold=True)
    page.block("headline", [caption])
    ids = [page.text(54, 740 - index * 16, 12, line) for index, line in enumerate(HYPHENATED)]
    page.block("body", ids)
    page.block("footer", [footer])
    page.notes["hyphenation"] = [
        {"lines": [ids[0], ids[1]], "joins": True, "word": "example"},
        {"lines": [ids[3], ids[4]], "joins": True, "word": "incremental"},
        {"lines": [ids[5], ids[6]], "joins": False, "word": "North-West",
         "why": "the next line begins with a capital letter"},
    ]
    page.notes["midlineHyphens"] = ["well-known"]
    catalog, _ = document(pdf, [page], resources)
    data = pdf.serialize(catalog, info(pdf, "Hyphenation", "Tomasz Wolny", "Soft hyphens",
                                       b"D:20260916160000Z"))
    return data, label_bytes(name, [page])


CJK_READABLE = ["文本提取与选择", "查找、搜索与比较", "中文排版测试页"]


@generator("text-tounicode.pdf")
def text_tounicode():
    """/ToUnicode on two kinds of font.

    Page 1, Latin: base-14 Helvetica with a /Differences encoding that puts the `fi` ligature glyph
    at TWO codes -- one mapped to U+FB01, one to the letters `f` `i` -- an `e` with a spacing acute
    kerned over it and mapped to U+0301 (a DECOMPOSED e-acute) beside WinAnsi's precomposed one, and
    the embedded Latin subset with a /ToUnicode of its own.

    Page 2, CJK: the same synthetic subset as `cjk-embedded.pdf`, the same Identity-H -- WITH a
    /ToUnicode. That file is the control this page answers: the same glyphs, one mappable and one
    not.

    The READABLE CJK -- a font the engine draws with the machine's own face -- is `cjk-readable.pdf`
    and not a third page here: the capture records a substituted face per DOCUMENT, so a third page
    made this file's Latin baseline machine-dependent (measured at Stage 5 T1)."""
    pdf = pb.Pdf()
    ligature_map = tounicode(pdf, [(code, chr(code)) for code in range(32, 127)]
                             + [(0x80, "ﬁ"), (0x81, "fi"), (0x82, "́"), (0xE9, "é")], 1)
    ligature_font = pdf.add(pb.dictionary([
        (b"/Type", b"/Font"), (b"/Subtype", b"/Type1"), (b"/BaseFont", b"/Helvetica"),
        (b"/Encoding", pb.dictionary([(b"/Type", b"/Encoding"),
                                      (b"/BaseEncoding", b"/WinAnsiEncoding"),
                                      (b"/Differences", pb.array([b"128", b"/fi", b"/fi",
                                                                  b"/acute"]))])),
        (b"/ToUnicode", pb.ref(ligature_map))]))

    font_bytes, mapping, widths = fontenc.latin_font()
    font_data, font_filter = pb.flate(font_bytes)
    font_file = pdf.add_stream([(b"/Length1", pb.num(len(font_bytes))),
                                (b"/Filter", font_filter)], font_data)
    descriptor = pdf.add(pb.dictionary([
        (b"/Type", b"/FontDescriptor"), (b"/FontName", pb.name("RawPageCorpusLatin")),
        (b"/Flags", b"32"), (b"/FontBBox", pb.rect(0, -120, 700, 700)),
        (b"/ItalicAngle", b"0"), (b"/Ascent", b"880"), (b"/Descent", b"-120"),
        (b"/CapHeight", b"700"), (b"/StemV", b"80"), (b"/FontFile2", pb.ref(font_file))]))
    first, last = min(mapping), max(mapping)
    latin = pdf.add(pb.dictionary([
        (b"/Type", b"/Font"), (b"/Subtype", b"/TrueType"),
        (b"/BaseFont", pb.name("RawPageCorpusLatin")),
        (b"/FirstChar", pb.num(first)), (b"/LastChar", pb.num(last)),
        (b"/Widths", pb.array([pb.num(widths[mapping[c]]) if c in mapping else b"0"
                               for c in range(first, last + 1)])),
        (b"/Encoding", b"/WinAnsiEncoding"), (b"/FontDescriptor", pb.ref(descriptor)),
        (b"/ToUnicode", pb.ref(tounicode(pdf, [(c, chr(c)) for c in sorted(mapping)], 1)))]))

    cjk, cjk_gids, _ = embedded_composite(pdf, fontenc.cjk_font, "RawPageCorpusCJK", b"/Identity-H")

    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")

    def own(*fonts):
        return pb.dictionary([(b"/Font", pb.dictionary(
            [(b"/F1", pb.ref(regular)), (b"/F2", pb.ref(bold))] + list(fonts)))])
    resources = [own((b"/F3", pb.ref(ligature_font)), (b"/F4", pb.ref(latin))),
                 own((b"/F5", pb.ref(cjk)))]
    name = "text-tounicode.pdf"
    size = 16.0

    # --- page 1: Latin
    latin_page = Page()
    footer1 = furniture(latin_page, name, 1, 2)
    caption = latin_page.text(54, 780, 11, "text-tounicode.pdf - ligatures, diacritics, /ToUnicode",
                              bold=True)
    latin_page.block("headline", [caption])
    ids = []

    def ligature_line(y, raw, logical, advance_text):
        latin_page.op(b"BT /F3 %s Tf 1 0 0 1 54 %s Tm %s Tj ET" % (pb.num(size), pb.num(y),
                                                                 pb.literal(raw)))
        return latin_page.record(logical, 54, 54 + width(advance_text, size), y, size)

    ids.append(ligature_line(720, b"\x80sh and chips", "ﬁsh and chips", "fish and chips"))
    ids.append(ligature_line(690, b"a \x81le of \x81gures", "a file of figures", "a file of figures"))
    ids.append(ligature_line(660, b"caf\xe9 au lait", "café au lait", "cafe au lait"))
    # the decomposed form: the spacing acute drawn back over the e, then the pen put back where
    # the e ended -- (556 + 333) / 2 back, then 556 - that - 333 forward, in thousandths of an em
    latin_page.op(b"BT /F3 %s Tf 1 0 0 1 54 630 Tm [ (cafe) 444.5 <82> -111.5 ( au lait) ] TJ ET"
                  % pb.num(size))
    ids.append(latin_page.record("café au lait", 54, 54 + width("cafe au lait", size), 630,
                                 size, decomposed=True))
    latin_page.op(b"BT /F4 %s Tf 1 0 0 1 54 590 Tm (LIFT THE FILE) Tj ET" % pb.num(24))
    ids.append(latin_page.record("LIFT THE FILE", 54, 54 + sum(
        widths[mapping[ord(c)]] for c in "LIFT THE FILE") * 24 / 1000.0, 590, 24,
        font="embedded subset"))
    latin_page.block("body", ids)
    latin_page.block("footer", [footer1])

    # --- page 2: CJK, embedded, with /ToUnicode
    cjk_page = Page()
    footer2 = furniture(cjk_page, name, 2, 2)
    caption = cjk_page.text(54, 780, 11, "page 2 - the cjk-embedded.pdf subset, with /ToUnicode",
                            bold=True)
    cjk_page.block("headline", [caption])
    ids = []
    for index, text in enumerate(["一二三十口日田中目", "中日目中日田", "十日中日口"]):
        y = 720 - index * 40
        cjk_page.op(glyph_run(b"/F5", 24, 54, y, [cjk_gids[c] for c in text]))
        ids.append(cjk_page.record(text, 54, 54 + 24 * len(text), y, 24))
    cjk_page.block("body", ids)
    cjk_page.block("footer", [footer2])

    pages = [latin_page, cjk_page]
    catalog, _ = document(pdf, pages, resources)
    data = pdf.serialize(catalog, info(pdf, "ToUnicode", "Chen Wei",
                                       "Ligatures, diacritics and CJK mappings",
                                       b"D:20260917093000Z"))
    return data, label_bytes(name, pages)


CJK_VERTICAL_READABLE = ["竖排文字测试", "从右向左阅读"]


@generator("cjk-readable.pdf")
def cjk_readable():
    """CJK a person can READ: a NON-embedded Adobe-GB1 font (/STSong-Light) through the predefined
    /UniGB-UCS2-H and /UniGB-UCS2-V CMaps, so the codes are Unicode and the engine draws the
    characters with the machine's own CJK face -- SimSun on the machine that captured it, which is
    why this file is the one Stage 5 golden case marked machine-dependent. Horizontal lines, then
    two vertical columns read right to left. The walkable twin of `cjk-embedded.pdf`,
    `text-tounicode.pdf` and `cjk-vertical.pdf`, whose nine rectangle hanzi are real characters and
    only nine (plan D5-23)."""
    pdf = pb.Pdf()
    descriptor = pdf.add(pb.dictionary([
        (b"/Type", b"/FontDescriptor"), (b"/FontName", b"/STSong-Light"), (b"/Flags", b"6"),
        (b"/FontBBox", pb.rect(-25, -254, 1000, 880)), (b"/ItalicAngle", b"0"),
        (b"/Ascent", b"880"), (b"/Descent", b"-120"), (b"/CapHeight", b"880"),
        (b"/StemV", b"93")]))
    cid = pdf.add(pb.dictionary([
        (b"/Type", b"/Font"), (b"/Subtype", b"/CIDFontType0"), (b"/BaseFont", b"/STSong-Light"),
        (b"/CIDSystemInfo", pb.dictionary([(b"/Registry", pb.literal(b"Adobe")),
                                           (b"/Ordering", pb.literal(b"GB1")),
                                           (b"/Supplement", b"4")])),
        (b"/FontDescriptor", pb.ref(descriptor)), (b"/DW", b"1000")]))

    def type0(encoding):
        return pdf.add(pb.dictionary([
            (b"/Type", b"/Font"), (b"/Subtype", b"/Type0"), (b"/BaseFont", b"/STSong-Light"),
            (b"/Encoding", encoding), (b"/DescendantFonts", pb.array([pb.ref(cid)]))]))

    resources = helvetica_resources(pdf, [(b"/F3", pb.ref(type0(b"/UniGB-UCS2-H"))),
                                          (b"/F4", pb.ref(type0(b"/UniGB-UCS2-V")))])
    page = Page()
    name = "cjk-readable.pdf"
    footer = furniture(page, name, 1, 1)
    caption = page.text(54, 780, 11, "cjk-readable.pdf - STSong-Light, not embedded, UniGB-UCS2",
                        bold=True)
    page.block("headline", [caption])

    def ucs2(text):
        return text.encode("utf-16-be").hex().upper().encode()

    ids = []
    for index, text in enumerate(CJK_READABLE):
        y = 720 - index * 40
        page.op(b"BT /F3 24 Tf 1 0 0 1 54 %s Tm <%s> Tj ET" % (pb.num(y), ucs2(text)))
        ids.append(page.record(text, 54, 54 + 24 * len(text), y, 24))
    page.block("body", ids)

    size = 24.0
    top = 560.0
    ids = []
    for index, text in enumerate(CJK_VERTICAL_READABLE):
        x = 420.0 - index * 50.0
        page.op(b"BT /F4 %s Tf 1 0 0 1 %s %s Tm <%s> Tj ET" % (pb.num(size), pb.num(x),
                                                              pb.num(top), ucs2(text)))
        ids.append(page.record(text, x - size / 2, x + size / 2, top, size, direction="ttb",
                               box=[r3(x - size / 2), r3(top - len(text) * size),
                                    r3(x + size / 2), r3(top)]))
    page.block("body", ids)
    page.block("footer", [footer])
    catalog, _ = document(pdf, [page], resources)
    data = pdf.serialize(catalog, info(pdf, "Readable CJK", "Chen Wei",
                                       "A non-embedded CJK font", b"D:20260917150000Z"))
    return data, label_bytes(name, [page], machineDependent="the characters are drawn with the "
                             "machine's own CJK face; SimSun where this was generated")


# The words each content class carries, and the ones that must be found NOWHERE. Chosen from outside
# pdfbuild's WORDS so no filler line can contain one by accident; A-14 checks the control word
# against every decoded byte of the file.
CLASS_WORDS = {
    "pageText": ["papaya"],
    "comments": ["quokka"],
    "bookmarks": ["zeppelin"],
    "fields": ["marmot", "walrus"],
    "attachments": ["lemur", "ocelot", "narwhal", "axolotl"],
}
NOT_FOUND = {
    "wombat": "the control: in no class and no byte of the file",
    "hedgehog": "the CONTENT of a .txt attachment -- a non-PDF attachment is searched by name and "
                "description only (plan D5-12)",
    "pangolin": "the text of a PDF attached INSIDE the attached PDF -- one level deep only (D5-12)",
}


def _tiny_pdf(text, title, attach=None):
    """A one-page PDF whose page says `text` -- the attachment payloads -- optionally carrying
    an attachment of its own."""
    pdf = pb.Pdf()
    font = pb.base14(pdf, "Helvetica")
    resources = pb.dictionary([(b"/Font", pb.dictionary([(b"/F1", pb.ref(font))]))])
    contents = pb.content_stream(pdf, pb.show_text(72, 720, 14, text) + b"\n")
    root_extra = []
    if attach is not None:
        root_extra.append((b"/Names", pb.dictionary([(b"/EmbeddedFiles", _embedded_files(
            pdf, [attach]))])))
    catalog, _, _ = pb.simple_document(
        pdf, [lambda parent: pb.page(parent, (300.0, 300.0), contents, resources)],
        root_extra=root_extra)
    return pdf.serialize(catalog, pb.info_object(pdf, title))


def _embedded_files(pdf, entries):
    """An /EmbeddedFiles name tree over [(name, bytes, mime, description or None)], sorted by name
    as a name tree must be."""
    pairs = []
    for filename, payload, mime, description in sorted(entries, key=lambda entry: entry[0]):
        data, filter_name = pb.flate(payload)
        stream = pdf.add_stream([(b"/Type", b"/EmbeddedFile"), (b"/Subtype", pb.name(mime)),
                                 (b"/Params", pb.dictionary([(b"/Size", pb.num(len(payload)))])),
                                 (b"/Filter", filter_name)], data)
        spec = [(b"/Type", b"/Filespec"), (b"/F", pb.literal(filename)),
                (b"/UF", pb.utf16(filename)),
                (b"/EF", pb.dictionary([(b"/F", pb.ref(stream))]))]
        if description is not None:
            spec.append((b"/Desc", pb.utf16(description)))
        pairs += [pb.literal(filename), pb.ref(pdf.add(pb.dictionary(spec)))]
    return pb.dictionary([(b"/Names", pb.array(pairs))])


@generator("search-classes.pdf")
def search_classes():
    """One unique word in each of the five content classes `Features.md` section 1.8 names, and
    words that must be found nowhere -- the matrix Stage 5's include-toggles are tested over."""
    pdf = pb.Pdf()
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    resources = pb.dictionary([(b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)),
                                                         (b"/F2", pb.ref(bold))]))])
    helv = pb.dictionary([(b"/Font", pb.dictionary([(b"/Helv", pb.ref(regular))]))])
    page_number = pdf.reserve()
    pages_number = pdf.reserve()
    name = "search-classes.pdf"

    page = Page()
    footer = furniture(page, name, 1, 1)
    caption = page.text(54, 780, 11, "search-classes.pdf - one word in each content class",
                        bold=True)
    page.block("headline", [caption])
    body = [page.text(54, 740, 11, "The page text of this file carries exactly one unusual"),
            page.text(54, 724, 11, "word of its own, and that word is papaya; every other"),
            page.text(54, 708, 11, "class carries its word somewhere a page never shows.")]
    page.block("body", body)
    page.block("footer", [footer])

    def appearance(w, h, data):
        encoded, filter_name = pb.flate(data)
        return pdf.add_stream([(b"/Type", b"/XObject"), (b"/Subtype", b"/Form"),
                               (b"/BBox", pb.rect(0, 0, w, h)), (b"/Resources", helv),
                               (b"/Filter", filter_name)], encoded)

    note = pdf.add(pb.dictionary([
        (b"/Type", b"/Annot"), (b"/Subtype", b"/Text"), (b"/Rect", pb.rect(500, 700, 520, 720)),
        (b"/F", b"4"), (b"/P", pb.ref(page_number)), (b"/Contents", pb.utf16("quokka")),
        (b"/Name", b"/Comment"),
        (b"/AP", pb.dictionary([(b"/N", pb.ref(appearance(
            20, 20, b"1 0.85 0.3 rg 0 0 20 20 re f 0 G 0.7 w 0.35 0.35 19.3 19.3 re S\n")))]))]))
    field_ap = appearance(200, 22, b"0.94 g 0 0 200 22 re f 0.3 G 0.5 w 0.25 0.25 199.5 21.5 re S "
                                   b"/Tx BMC q BT /Helv 10 Tf 0 g 1 0 0 1 3 7.4 Tm (walrus) Tj ET "
                                   b"Q EMC\n")
    field = pdf.add(pb.dictionary([
        (b"/Type", b"/Annot"), (b"/Subtype", b"/Widget"), (b"/FT", b"/Tx"),
        (b"/Rect", pb.rect(54, 620, 254, 642)), (b"/T", pb.utf16("marmot")),
        (b"/V", pb.utf16("walrus")), (b"/F", b"4"), (b"/P", pb.ref(page_number)),
        (b"/DA", pb.literal(b"/Helv 10 Tf 0 g")),
        (b"/AP", pb.dictionary([(b"/N", pb.ref(field_ap))]))]))

    contents = pb.content_stream(pdf, page.content())
    pdf.set(page_number, pb.page(pages_number, pb.A4, contents, resources,
                                 [(b"/Annots", pb.array([pb.ref(note), pb.ref(field)]))]))
    pdf.set(pages_number, pb.dictionary([(b"/Type", b"/Pages"),
                                         (b"/Kids", pb.array([pb.ref(page_number)])),
                                         (b"/Count", b"1")]))

    outlines = pdf.reserve()
    item = pdf.add(pb.dictionary([
        (b"/Title", pb.utf16("zeppelin")), (b"/Parent", pb.ref(outlines)),
        (b"/Dest", pb.array([pb.ref(page_number), b"/Fit"]))]))
    pdf.set(outlines, pb.dictionary([(b"/Type", b"/Outlines"), (b"/First", pb.ref(item)),
                                     (b"/Last", pb.ref(item)), (b"/Count", b"1")]))

    nested = _tiny_pdf("pangolin", "RawPage corpus: nested attachment")
    lemur = _tiny_pdf("axolotl", "RawPage corpus: attached PDF",
                      attach=("nested.pdf", nested, "application/pdf", None))
    embedded = _embedded_files(pdf, [
        ("ocelot.txt", b"hedgehog -- the content of a text attachment\n", "text/plain", "narwhal"),
        ("lemur.pdf", lemur, "application/pdf", None)])
    acroform = pb.dictionary([(b"/Fields", pb.array([pb.ref(field)])), (b"/DR", helv),
                              (b"/DA", pb.literal(b"/Helv 10 Tf 0 g"))])
    catalog = pdf.add(pb.dictionary([
        (b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number)),
        (b"/Outlines", pb.ref(outlines)), (b"/AcroForm", acroform),
        (b"/Names", pb.dictionary([(b"/EmbeddedFiles", embedded)]))]))
    data = pdf.serialize(catalog, info(pdf, "Search classes", "Noa Levi",
                                       "Five content classes", b"D:20260918120000Z"))
    return data, label_bytes(name, [page], classes=CLASS_WORDS, notFound=NOT_FOUND)


TEXT_900_BODIES = 10


def _dense_body(index):
    """About 6,500 characters -- a2-text-page.pdf's density -- wrapped to the A4 text width."""
    words = pb.words_for(90000 + index * 977, 1000)
    return wrap(words, 9, 487)[:68]


@generator("text-900.pdf")
def text_900():
    """900 dense pages: page N's text is a marker line naming N, then body N mod 10. The bodies
    are shared content streams, so the file stays small while the extraction work is real -- the
    workload A2 at Stage 5 is measured on (plan D5-22) and the one *"the second search of a
    900-page file is instant"* is defined over (guide section 3.3). The label holds the ten bodies
    and the marker's form, from which every page's text follows, plus pages 1, 2 and 900 written
    out."""
    pdf = pb.Pdf()
    resources = helvetica_resources(pdf)
    bodies = []
    body_streams = []
    for index in range(TEXT_900_BODIES):
        lines = _dense_body(index)
        bodies.append(lines)
        ops = [pb.show_text(54, 790 - row * 11.2, 9, line) for row, line in enumerate(lines)]
        body_streams.append(pb.content_stream(pdf, b"\n".join(ops) + b"\n"))
    marker_form = "unique-page-%04d of 900 -- body %d"

    pages_root = pdf.reserve()
    kids = []
    page_numbers = []
    for group in range(30):
        node = pdf.reserve()
        leaves = []
        for offset in range(30):
            index = group * 30 + offset
            marker = pb.content_stream(pdf, pb.show_text(54, 815, 8, marker_form % (
                index + 1, index % TEXT_900_BODIES)) + b"\n", mode="stored")
            leaves.append(pdf.add(pb.dictionary([
                (b"/Type", b"/Page"), (b"/Parent", pb.ref(node)),
                (b"/MediaBox", pb.rect(0, 0, pb.A4[0], pb.A4[1])), (b"/Resources", resources),
                (b"/Contents", pb.array([pb.ref(marker),
                                         pb.ref(body_streams[index % TEXT_900_BODIES])]))])))
        page_numbers.extend(leaves)
        pdf.set(node, pb.dictionary([(b"/Type", b"/Pages"), (b"/Parent", pb.ref(pages_root)),
                                     (b"/Kids", pb.array([pb.ref(n) for n in leaves])),
                                     (b"/Count", b"30")]))
        kids.append(node)
    pdf.set(pages_root, pb.dictionary([(b"/Type", b"/Pages"),
                                       (b"/Kids", pb.array([pb.ref(n) for n in kids])),
                                       (b"/Count", b"900")]))
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_root))]))
    data = pdf.serialize(catalog, info(pdf, "900 dense pages", "Tomasz Wolny",
                                       "The Stage 5 search workload", b"D:20260919070000Z"),
                         objstm=True)

    def written(index):
        page = Page()
        marker_id = page.record(marker_form % (index + 1, index % TEXT_900_BODIES), 54,
                                54 + width(marker_form % (index + 1, index % TEXT_900_BODIES), 8),
                                815, 8)
        page.block("header", [marker_id])
        body_ids = []
        for row, line in enumerate(bodies[index % TEXT_900_BODIES]):
            body_ids.append(page.record(line, 54, 54 + width(line, 9), 790 - row * 11.2, 9))
        page.block("body", body_ids)
        return page.label(index)

    label = {"file": "ordinary/text-900.pdf", "generator": "corpus/make-text.py",
             "units": "PDF points, page space",
             "formula": {"pages": 900,
                         "marker": "unique-page-{number:04d} of 900 -- body {body}",
                         "number": "page index + 1", "body": "page index mod %d" % TEXT_900_BODIES,
                         "markerBaseline": 815, "markerSize": 8, "bodyTop": 790,
                         "bodyLeading": 11.2, "bodySize": 9, "bodies": bodies},
             "pages": [written(0), written(1), written(899)]}
    return data, (json.dumps(label, ensure_ascii=False, indent=1) + "\n").encode("utf-8")


# ---------------------------------------------------------------------------------------------

def generate():
    out = {}
    for name, builder in FILES.items():
        data, label = builder()
        out["ordinary/" + name] = data
        out["labels/" + name.replace(".pdf", ".json")] = label
    return out


if __name__ == "__main__":
    root = pathlib.Path(__file__).resolve().parent
    for relative, data in generate().items():
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        print("wrote %-34s %8d bytes" % (relative, len(data)))
