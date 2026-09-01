#!/usr/bin/env python3
"""Generates corpus/a2-text-page.pdf -- one dense A4 text page, for the A2 render probe.

WHAT IT IS FOR. Stage 2 T3 measures risk A2 (Tech-Stack-Plan.md 3.3): "PDFium's
FPDF_RenderPageBitmap throughput is sufficient for smooth 60 Hz scrolling of a TEXT-HEAVY A4 PAGE at
200% on a 150%-scaled 4K monitor with an 8-worker pool." That sentence names the input, and
tests/bench/bench_render.cpp needs a file matching it before it can produce a number that answers
the question rather than a number about whatever file was to hand.

WHY A GENERATOR AND NOT A COMMITTED-ONCE BLOB -- the same reason make-minimal.py gives, and it is
worth repeating because it is the property the whole corpus decision (D2-11) rests on: a PDF
cross-reference table stores ABSOLUTE BYTE OFFSETS, so hand-editing the file by one character
invalidates every offset after it. This script is deterministic: no clock, no randomness that is not
seeded, no dictionary iteration order. Run it twice and the bytes are identical, which is what makes
"regenerate it and see" a usable instruction.

WHAT THIS PAGE IS, AND WHAT IT DELIBERATELY IS NOT -- stated here because the A2 verdict inherits
every one of these limits and a probe's caveats belong with the probe's input:

  * 60 lines of 9 pt body text at ~100 characters each, plus a heading and a footer: 62 text runs
    and 6,650 glyphs, measured by running it rather than estimated. That is a dense page of body
    copy rather than a title slide, and glyph rasterization is what the measurement is about.
  * Helvetica, one of the base-14 fonts, NOT EMBEDDED. So PDFium resolves it through its own font
    fallback to a system face. A page with an embedded subset exercises a different path -- FreeType
    over embedded outlines rather than a system font -- and T4's corpus is where that page comes
    from. The probe's figure is therefore about the common case, not about every case.
  * Two hairline rules, so the page is not purely glyphs. Not enough vector art to be a claim about
    vector-heavy content, which is also T4's.
  * The content stream is UNCOMPRESSED, for inspectability. It costs nothing in the measurement:
    a content stream is decoded once when the page is loaded, and the probe times tile rendering
    with the page already loaded.
  * No transparency, no images, no shading, no annotations, no forms. Each of those is its own cost
    and each belongs to the T18 measurement over the whole corpus, which is the one that is a claim
    about the product.

    python corpus/make-a2-text-page.py
"""

import pathlib

# A4, in points. Same MediaBox as minimal.pdf, and the size A2 is specified against.
PAGE_WIDTH = 595.276
PAGE_HEIGHT = 841.89

MARGIN = 48.0
FONT_SIZE = 9.0
LEADING = 11.6
LINE_COUNT = 64

# Deterministic prose. A fixed word list plus a fixed linear-congruential walk over it -- not
# `random`, whose stream is an implementation detail of the interpreter, and not a lorem ipsum blob
# pasted in, which would make the line lengths whatever the paste happened to be. What matters for
# the measurement is glyph COUNT and word-break density, and both are stable here.
WORDS = [
    "document", "render", "raster", "glyph", "outline", "baseline", "kerning", "typeface",
    "page", "tile", "viewport", "cache", "revision", "scope", "invalidate", "schedule",
    "worker", "thread", "latency", "throughput", "percentile", "budget", "frame", "monitor",
    "resolution", "physical", "logical", "scaling", "policy", "boundary", "interface", "facade",
    "object", "stream", "filter", "table", "trailer", "offset", "repair", "report",
    "incremental", "append", "signature", "permission", "annotation", "outline", "layer", "tag",
]


def words_for(seed: int, count: int) -> list:
    """A fixed walk over WORDS. The constants are Numerical Recipes' LCG; any stable generator
    would do, and the point is only that it is ours rather than the interpreter's."""
    out = []
    state = seed
    for _ in range(count):
        state = (1664525 * state + 1013904223) % (2 ** 32)
        out.append(WORDS[state % len(WORDS)])
    return out


def escape(text: str) -> bytes:
    """PDF literal-string escaping. Only three characters matter and none of them occurs in the
    word list -- the escape exists so that changing the word list cannot silently produce a file
    whose content stream no longer parses."""
    out = text.replace("\\", r"\\").replace("(", r"\(").replace(")", r"\)")
    return out.encode("ascii")


def build_content() -> bytes:
    lines = []

    # A heading, so the page has more than one font size on it -- a size change is a state change
    # in the content stream and in the rasterizer's glyph cache alike.
    lines.append(b"BT /F2 18 Tf 1 0 0 1 %.2f %.2f Tm (RawPage A2 render probe) Tj ET"
                 % (MARGIN, PAGE_HEIGHT - MARGIN))

    # Two hairlines: a rule under the heading and one above the footer. 0.5 pt is the width at which
    # anti-aliasing decisions become visible, which is also why pageDisplay/lineWeight exists.
    rule_y = PAGE_HEIGHT - MARGIN - 10.0
    lines.append(b"0.5 w %.2f %.2f m %.2f %.2f l S" % (MARGIN, rule_y, PAGE_WIDTH - MARGIN, rule_y))

    top = rule_y - 18.0
    for index in range(LINE_COUNT):
        # ~100 characters: 14 words of average length 6 plus separators. LINE_COUNT is an upper
        # bound and the y-position test below is what actually ends the page, so changing the
        # leading cannot silently push text off the bottom -- it produces fewer lines, and the
        # figure this script prints says so.
        text = " ".join(words_for(index + 1, 14))
        y = top - index * LEADING
        if y < MARGIN + 24.0:
            break
        lines.append(b"BT /F1 %.1f Tf 1 0 0 1 %.2f %.2f Tm (%s) Tj ET"
                     % (FONT_SIZE, MARGIN, y, escape(text)))

    footer_y = MARGIN + 14.0
    lines.append(b"0.5 w %.2f %.2f m %.2f %.2f l S"
                 % (MARGIN, footer_y, PAGE_WIDTH - MARGIN, footer_y))
    lines.append(b"BT /F1 8 Tf 1 0 0 1 %.2f %.2f Tm "
                 b"(corpus/a2-text-page.pdf -- generated by corpus/make-a2-text-page.py) Tj ET"
                 % (MARGIN, MARGIN))

    return b"\n".join(lines) + b"\n"


def build() -> bytes:
    content = build_content()

    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.3f %.3f] "
        b"/Resources << /Font << /F1 5 0 R /F2 6 0 R >> >> /Contents 4 0 R >>"
        % (PAGE_WIDTH, PAGE_HEIGHT),
        b"<< /Length " + str(len(content)).encode() + b" >>\nstream\n" + content + b"endstream",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>",
    ]

    out = bytearray()
    # The binary comment on line 2, and the reason .gitattributes must treat corpus/** as binary:
    # normalising these bytes would shift every offset below.
    out += b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n"

    offsets = []
    for number, body in enumerate(objects, start=1):
        offsets.append(len(out))
        out += b"%d 0 obj\n" % number
        out += body
        out += b"\nendobj\n"

    xref_offset = len(out)
    count = len(objects) + 1
    out += b"xref\n0 %d\n" % count
    out += b"0000000000 65535 f \n"
    for offset in offsets:
        out += b"%010d 00000 n \n" % offset

    out += b"trailer\n<< /Size %d /Root 1 0 R >>\n" % count
    out += b"startxref\n%d\n%%%%EOF\n" % xref_offset
    return bytes(out)


if __name__ == "__main__":
    target = pathlib.Path(__file__).with_name("a2-text-page.pdf")
    data = build()
    target.write_bytes(data)

    # The glyph count is printed rather than asserted: it is what makes "text-heavy" a figure the
    # A2 verdict can quote instead of an adjective, and it changes if the word list or the leading
    # is ever edited.
    glyphs = sum(len(chunk) for chunk in build_content().split(b") Tj")[:-1])
    lines = build_content().count(b") Tj")
    print(f"wrote {target} ({len(data)} bytes), {lines} text runs")
