#!/usr/bin/env python3
"""A synthetic TrueType font builder, for the corpus's embedded-font files.

WHY. D2-11 requires an embedded CJK subset in the corpus and requires every corpus byte to be ours.
Shipping a real typeface would be a licensed third-party file -- exactly what the decision rules
out, and the one kind of corpus content `license-audit` does not govern (it audits linked
dependencies, not data). So the font is built here.

WHAT IS EXERCISED, and what is not. An embedded font takes a completely different path through the
rasterizer from a base-14 name: PDFium parses /FontFile2 and hands the outlines to FreeType, rather
than resolving a name against a system face. That path -- glyf/loca/cmap/hmtx parsing, CIDFontType2
with Identity-H, /CIDToGIDMap, the /W width array, /FontDescriptor's metrics -- is what these files
are for, and every part of it is real here.

WHAT IS DELIBERATELY CRUDE. Every glyph is built from axis-aligned rectangles: no curves, no
hinting, no kerning, no composite glyphs, and the CJK glyphs are the handful of characters that
genuinely ARE straight strokes (一 二 三 十 口 日 田 中 目). They are real characters correctly
drawn, not placeholder boxes -- but a font of nine rectilinear glyphs is not a claim about
typographic fidelity, and `corpus/PROVENANCE.md` says so.

DETERMINISM. Integer coordinates, a fixed (zero) creation date in `head`, and tables written in a
fixed order. Nothing here reads a clock or a locale.
"""

import struct

UNITS_PER_EM = 1000
ASCENT = 880
DESCENT = -120

# Glyphs as lists of (x0, y0, x1, y1) rectangles in font units. Every one of these characters is
# built entirely from straight horizontal and vertical strokes in real type, which is exactly why
# this set was chosen -- a rectangle font can draw them correctly rather than approximately.
CJK_GLYPHS = [
    ("一", [(100, 430, 900, 510)]),                                   # yi   -- one
    ("二", [(150, 250, 850, 320), (150, 620, 850, 690)]),             # er   -- two
    ("三", [(140, 170, 860, 240), (200, 430, 800, 500),
                (120, 690, 880, 760)]),                                   # san  -- three
    ("十", [(100, 430, 900, 500), (465, 90, 535, 840)]),              # shi  -- ten
    ("口", [(180, 160, 250, 760), (750, 160, 820, 760),
                (180, 690, 820, 760), (180, 160, 820, 230)]),             # kou  -- mouth
    ("日", [(240, 130, 310, 800), (690, 130, 760, 800),
                (240, 730, 760, 800), (240, 130, 760, 200),
                (240, 430, 760, 500)]),                                   # ri   -- sun
    ("田", [(150, 130, 220, 800), (780, 130, 850, 800),
                (150, 730, 850, 800), (150, 130, 850, 200),
                (150, 430, 850, 500), (465, 130, 535, 800)]),             # tian -- field
    ("中", [(250, 230, 320, 700), (680, 230, 750, 700),
                (250, 630, 750, 700), (250, 230, 750, 300),
                (465, 60, 535, 880)]),                                    # zhong -- middle
    ("目", [(280, 120, 350, 810), (650, 120, 720, 810),
                (280, 740, 720, 810), (280, 120, 720, 190),
                (280, 350, 720, 410), (280, 545, 720, 605)]),             # mu   -- eye
]

# Latin block capitals, likewise rectangles only. The word the corpus writes with them --
# "LIFT THE FILE" -- uses exactly these seven glyphs, which is why they are the ones drawn.
LATIN_GLYPHS = [
    (" ", []),
    ("E", [(120, 0, 200, 700), (120, 620, 620, 700), (120, 310, 540, 390),
           (120, 0, 620, 80)]),
    ("F", [(120, 0, 200, 700), (120, 620, 620, 700), (120, 310, 540, 390)]),
    ("H", [(120, 0, 200, 700), (560, 0, 640, 700), (120, 310, 640, 390)]),
    ("I", [(280, 0, 360, 700)]),
    ("L", [(120, 0, 200, 700), (120, 0, 620, 80)]),
    ("T", [(80, 620, 660, 700), (330, 0, 410, 700)]),
]


def _checksum(data):
    padded = data + b"\x00" * ((4 - len(data) % 4) % 4)
    total = 0
    for index in range(0, len(padded), 4):
        total = (total + struct.unpack(">I", padded[index:index + 4])[0]) & 0xFFFFFFFF
    return total


def _glyph(rectangles):
    """One simple glyph. Contours run clockwise in a y-up space, which is TrueType's fill
    convention -- reversed, the non-zero winding rule makes every rectangle a hole."""
    if not rectangles:
        return b""                                  # an empty glyph is a zero-length loca entry
    xs = [value for rect in rectangles for value in (rect[0], rect[2])]
    ys = [value for rect in rectangles for value in (rect[1], rect[3])]

    points = []
    end_points = []
    for x0, y0, x1, y1 in rectangles:
        points.extend([(x0, y0), (x0, y1), (x1, y1), (x1, y0)])
        end_points.append(len(points) - 1)

    out = bytearray()
    out += struct.pack(">hhhhh", len(rectangles), min(xs), min(ys), max(xs), max(ys))
    for value in end_points:
        out += struct.pack(">H", value)
    out += struct.pack(">H", 0)                     # no instructions
    out += bytes([0x01]) * len(points)              # every point on-curve, no repeats, 16-bit deltas

    previous = 0
    for x, _ in points:
        out += struct.pack(">h", x - previous)
        previous = x
    previous = 0
    for _, y in points:
        out += struct.pack(">h", y - previous)
        previous = y
    if len(out) % 4:
        out += b"\x00" * (4 - len(out) % 4)
    return bytes(out)


def _cmap_format4(mapping):
    """A (3,1) format-4 subtable. Present even in the CID font, where Identity-H means PDF never
    consults it -- because a TrueType without a Windows cmap is rejected by enough tools that its
    absence would make the fixture about tool tolerance rather than about the embedding path."""
    segments = []
    for code in sorted(mapping):
        glyph = mapping[code]
        if segments and segments[-1][1] == code - 1 and segments[-1][2] + (code - segments[-1][0]) == glyph:
            segments[-1] = (segments[-1][0], code, segments[-1][2])
        else:
            segments.append((code, code, glyph))
    segments.append((0xFFFF, 0xFFFF, 0))

    count = len(segments)
    search_range = 2
    entry_selector = 0
    while search_range * 2 <= count * 2:
        search_range *= 2
        entry_selector += 1

    arrays = bytearray()
    for _, end, _ in segments:
        arrays += struct.pack(">H", end)
    arrays += struct.pack(">H", 0)                  # reservedPad
    for start, _, _ in segments:
        arrays += struct.pack(">H", start)
    for start, _, glyph in segments:
        delta = 1 if start == 0xFFFF else (glyph - start) & 0xFFFF
        arrays += struct.pack(">H", delta)
    for _ in segments:
        arrays += struct.pack(">H", 0)              # idRangeOffset: all deltas, no glyph array

    subtable = struct.pack(">HHHHHHH", 4, 14 + len(arrays), 0, count * 2,
                           search_range, entry_selector, count * 2 - search_range) + bytes(arrays)
    header = struct.pack(">HHHHI", 0, 1, 3, 1, 12)
    return header + subtable


def _name_table(family):
    """A minimal `name` table: family, subfamily, full name, PostScript name, all Windows/UTF-16BE.
    A PDF /FontDescriptor names the font too, and a mismatch between the two is a real-world
    condition -- so they are kept consistent here on purpose, and the mismatch is not simulated."""
    records = [(1, family), (2, "Regular"), (4, family), (6, family.replace(" ", ""))]
    strings = bytearray()
    entries = bytearray()
    for name_id, text in records:
        encoded = text.encode("utf-16-be")
        entries += struct.pack(">HHHHHH", 3, 1, 0x0409, name_id, len(encoded), len(strings))
        strings += encoded
    header = struct.pack(">HHH", 0, len(records), 6 + 12 * len(records))
    return header + bytes(entries) + bytes(strings)


def build(glyphs, family, notdef_width=500):
    """Assemble a TrueType font.

    `glyphs` is a list of (character, rectangles). Glyph 0 is .notdef and is added here. Returns
    (font bytes, {codepoint: glyph index}, [advance widths]).
    """
    entries = [("", [(60, 0, 140, 700), (60, 620, 560, 700), (480, 0, 560, 700),
                     (60, 0, 560, 80)])] + list(glyphs)

    glyph_data = bytearray()
    offsets = [0]
    widths = []
    for index, (character, rectangles) in enumerate(entries):
        glyph_data += _glyph(rectangles)
        offsets.append(len(glyph_data))
        if index == 0:
            widths.append(notdef_width)
        elif rectangles:
            widths.append(max(rect[2] for rect in rectangles) + 100)
        else:
            widths.append(500)
    mapping = {ord(character): index for index, (character, _) in enumerate(entries) if character}

    loca = b"".join(struct.pack(">I", offset) for offset in offsets)
    hmtx = b"".join(struct.pack(">Hh", widths[index], 0) for index in range(len(entries)))

    all_rects = [rect for _, rectangles in entries for rect in rectangles]
    x_min = min(rect[0] for rect in all_rects)
    y_min = min(rect[1] for rect in all_rects)
    x_max = max(rect[2] for rect in all_rects)
    y_max = max(rect[3] for rect in all_rects)

    head = struct.pack(
        ">IIIIHHqqhhhhHHhhh",
        0x00010000,          # version
        0x00010000,          # fontRevision
        0,                   # checkSumAdjustment, filled in below
        0x5F0F3CF5,          # magicNumber
        0x000B,              # flags
        UNITS_PER_EM,
        0, 0,                # created / modified -- a fixed epoch, never the clock
        x_min, y_min, x_max, y_max,
        0,                   # macStyle
        8,                   # lowestRecPPEM
        2,                   # fontDirectionHint
        1,                   # indexToLocFormat: long offsets
        0)                   # glyphDataFormat

    hhea = struct.pack(">IhhhHhhhhhhhhhhhH",
                       0x00010000, ASCENT, DESCENT, 0,
                       max(widths), 0, 0, x_max,
                       1, 0, 0, 0, 0, 0, 0, 0, len(entries))

    # maxp version 1.0: numGlyphs then thirteen more counts. maxZones is 2 for a font with a
    # twilight zone, which is what every rasterizer expects even from an unhinted font.
    maxp = struct.pack(">IHHHHHHHHHHHHHH",
                       0x00010000, len(entries),
                       max((len(rectangles) * 4 for _, rectangles in entries), default=0),
                       max((len(rectangles) for _, rectangles in entries), default=0),
                       0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)

    os2 = (struct.pack(">HhHHH", 4, 500, 400, 5, 0)
           + struct.pack(">hhhhhhhhhhh", 650, 650, 0, 75, 650, 650, 0, 350, 50, 250, 0)
           + b"\x00" * 10                                    # panose: unclassified
           + struct.pack(">IIII", 0, 0, 0, 0)                # ulUnicodeRange 1..4
           + b"RAWP"                                         # achVendID
           + struct.pack(">HHH", 0x0040, min(mapping), min(max(mapping), 0xFFFF))
           + struct.pack(">hhh", ASCENT, DESCENT, 0)
           + struct.pack(">HH", ASCENT, -DESCENT)
           + struct.pack(">II", 0, 0)                        # ulCodePageRange 1..2
           + struct.pack(">hh", 500, 700)                    # sxHeight, sCapHeight
           + struct.pack(">HHH", 0, 32, 1))

    post = struct.pack(">IIhhIIIII", 0x00030000, 0, 0, 0, 0, 0, 0, 0, 0)

    tables = [
        (b"OS/2", os2),
        (b"cmap", _cmap_format4(mapping)),
        (b"glyf", bytes(glyph_data)),
        (b"head", head),
        (b"hhea", hhea),
        (b"hmtx", hmtx),
        (b"loca", loca),
        (b"maxp", maxp),
        (b"name", _name_table(family)),
        (b"post", post),
    ]

    count = len(tables)
    search_range = 16
    entry_selector = 0
    while search_range * 2 <= count * 16:
        search_range *= 2
        entry_selector += 1
    font = bytearray(struct.pack(">IHHHH", 0x00010000, count, search_range, entry_selector,
                                 count * 16 - search_range))
    offset = 12 + count * 16
    directory = bytearray()
    body = bytearray()
    head_offset = None
    for tag, data in tables:
        padded = data + b"\x00" * ((4 - len(data) % 4) % 4)
        directory += tag + struct.pack(">III", _checksum(data), offset, len(data))
        if tag == b"head":
            head_offset = offset
        body += padded
        offset += len(padded)
    font += directory + body

    # head.checkSumAdjustment: 0xB1B0AFBA minus the checksum of the whole file with the field zero.
    adjustment = (0xB1B0AFBA - _checksum(bytes(font))) & 0xFFFFFFFF
    font[head_offset + 8:head_offset + 12] = struct.pack(">I", adjustment)

    return bytes(font), mapping, widths


def cjk_font():
    """The embedded CJK subset: nine rectilinear hanzi plus .notdef."""
    return build(CJK_GLYPHS, "RawPage Corpus CJK")


def latin_font():
    """The embedded Latin subset: seven block capitals plus .notdef."""
    return build(LATIN_GLYPHS, "RawPage Corpus Latin")


def selfcheck(font, mapping):
    """Parse the font back with FreeType through Pillow, when it is importable, and confirm the
    glyphs are reachable. Opportunistic -- its absence is reported, never silently skipped."""
    try:
        import io

        from PIL import ImageFont
    except ImportError:
        return "font self-check SKIPPED (Pillow not importable)"
    try:
        loaded = ImageFont.truetype(io.BytesIO(font), 48)
    except OSError as error:
        raise AssertionError("FreeType refused the generated font: %s" % error)
    text = "".join(chr(code) for code in sorted(mapping))
    width = loaded.getlength(text)
    if width <= 0:
        raise AssertionError("FreeType measured the whole glyph set at zero width")
    return "font self-check ok (FreeType loaded it; %d glyphs, %.0f units wide)" % (
        len(mapping), width)


if __name__ == "__main__":
    for maker in (cjk_font, latin_font):
        font, mapping, widths = maker()
        assert (font, mapping, widths) == maker(), "not deterministic"
        print("%-6s %5d bytes, %d glyphs; %s" % (
            maker.__name__, len(font), len(mapping), selfcheck(font, mapping)))
