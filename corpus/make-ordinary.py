#!/usr/bin/env python3
"""Generates `corpus/ordinary/` -- every well-formed file in the corpus.

WHAT "ORDINARY" MEANS HERE. Not "simple": these files are the ones that are supposed to OPEN and
RENDER, as opposed to `corpus/malformed/`, which is supposed to be diagnosed, and
`corpus/encrypted/`, which is supposed to ask for a password. Several of them are deliberately
awkward -- 900 pages, an odd page count with a cover, a right-to-left document, three incremental
generations -- because D2-11 (`Stage2-Rendering-Plan.md` section 3) holds the generators to a bar
above "emit a text page", on the reasoning that a construct the corpus never contains is a
construct nothing in this project ever renders.

WHAT EACH FILE IS FOR is recorded once, in `corpus/PROVENANCE.md`, and not restated here. What IS
here is why a file is built the way it is where that is not obvious from its name.

    python corpus/make-ordinary.py            # write them
    python corpus/make-corpus.py --verify     # prove they regenerate byte-identically
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import ccitt                                                          # noqa: E402
import fontenc                                                        # noqa: E402
import jpegenc                                                        # noqa: E402
import jpxenc                                                         # noqa: E402
import pdfbuild as pb                                                 # noqa: E402

FILES = {}


def generator(name):
    def register(function):
        FILES[name] = function
        return function
    return register


# ---------------------------------------------------------------------------------------------
# Shared page furniture
# ---------------------------------------------------------------------------------------------

def _label(text, size=14, y=None, media=pb.A4):
    """A caption at the top of a page. Every corpus page says what it is, in the page itself --
    a fixture you have to consult a table to identify is a fixture that gets misfiled."""
    return pb.show_text(48, (y if y is not None else media[1] - 60), size, text, b"/F2")


def _body_block(seed, lines, media=pb.A4, top=None, size=9, leading=11.6):
    out = []
    start = top if top is not None else media[1] - 96
    for index, line in enumerate(pb.text_body(seed, lines)):
        y = start - index * leading
        if y < 48:
            break
        out.append(pb.show_text(48, y, size, line))
    return out


def _fonts(pdf):
    return pdf, pb.base14(pdf, "Helvetica"), pb.base14(pdf, "Helvetica-Bold")


def _font_resource(regular, bold):
    return pb.dictionary([(b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)),
                                                    (b"/F2", pb.ref(bold))]))])


# ---------------------------------------------------------------------------------------------
# Text
# ---------------------------------------------------------------------------------------------

@generator("text-a4.pdf")
def text_a4():
    """Three A4 pages of body copy. Page 1's content stream is UNCOMPRESSED on purpose: with every
    stream flate-coded there would be no file in the corpus a person can read in a hex editor, and
    the first thing anyone does with a rendering bug is look at the content stream."""
    pdf = pb.Pdf()
    _, regular, bold = _fonts(pdf)
    resources = _font_resource(regular, bold)

    pages = []
    for index in range(3):
        lines = [_label("RawPage corpus - text-a4.pdf - page %d of 3" % (index + 1))]
        lines += _body_block(index + 1, 58)
        stream = b"\n".join(lines) + b"\n"
        contents = pb.content_stream(pdf, stream, compressed=(index > 0))
        pages.append(lambda parent, c=contents: pb.page(parent, pb.A4, c, resources))

    catalog, _, _ = pb.simple_document(pdf, pages)
    info = pb.info_object(pdf, "RawPage corpus: plain A4 text")
    return pdf.serialize(catalog, info)


@generator("text-letter.pdf")
def text_letter():
    """US Letter rather than A4 -- a different /MediaBox is the cheapest thing that breaks a
    layout engine that assumed one page size, and `PageLayoutEngine` (T13) is about to be written."""
    pdf = pb.Pdf()
    _, regular, bold = _fonts(pdf)
    resources = _font_resource(regular, bold)
    pages = []
    for index in range(2):
        lines = [_label("RawPage corpus - text-letter.pdf - page %d of 2" % (index + 1),
                        media=pb.LETTER)]
        lines += _body_block(10 + index, 50, media=pb.LETTER)
        contents = pb.content_stream(pdf, b"\n".join(lines) + b"\n")
        pages.append(lambda parent, c=contents: pb.page(parent, pb.LETTER, c, resources))
    catalog, _, _ = pb.simple_document(pdf, pages)
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: US Letter text"))


@generator("mixed-page-sizes.pdf")
def mixed_page_sizes():
    """A4 portrait, A4 landscape and A5 in one document, plus a page whose /MediaBox has a
    non-zero origin -- which is legal, common in imposed print files, and the case a layout that
    treats the box's width as its right edge gets wrong."""
    pdf = pb.Pdf()
    _, regular, bold = _fonts(pdf)
    resources = _font_resource(regular, bold)

    sizes = [("A4 portrait", pb.A4), ("A4 landscape", (pb.A4[1], pb.A4[0])), ("A5", pb.A5)]
    pages = []
    for title, media in sizes:
        content = b"\n".join([_label("mixed-page-sizes.pdf - " + title, media=media)]
                             + _body_block(20, 12, media=media)) + b"\n"
        contents = pb.content_stream(pdf, content)
        pages.append(lambda parent, c=contents, m=media: pb.page(parent, m, c, resources))

    shifted = pb.content_stream(pdf, pb.show_text(148, 500, 14,
                                                  "offset MediaBox origin (100 100)", b"/F2") + b"\n")
    pages.append(lambda parent, c=shifted: pb.dictionary([
        (b"/Type", b"/Page"), (b"/Parent", pb.ref(parent)),
        (b"/MediaBox", pb.rect(100, 100, 100 + pb.A4[0], 100 + pb.A4[1])),
        (b"/Resources", resources), (b"/Contents", pb.ref(c))]))

    catalog, _, _ = pb.simple_document(pdf, pages)
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: mixed page sizes"))


@generator("rotated-pages.pdf")
def rotated_pages():
    """/Rotate 0, 90, 180, 270 with identical content on each page. The content is deliberately
    the same so the only difference between the four rendered pages is the rotation -- which is
    what makes a golden baseline over this file mean something."""
    pdf = pb.Pdf()
    _, regular, bold = _fonts(pdf)
    resources = _font_resource(regular, bold)
    body = b"\n".join([
        _label("rotated-pages.pdf"),
        pb.show_text(48, 700, 11, "the arrow below points to the TOP of the unrotated page"),
        b"2 w 300 640 m 300 740 l S",
        b"280 720 m 300 745 l 320 720 l S",
        pb.show_text(48, 120, 9, "bottom-left corner marker"),
        b"1 w 40 40 m 90 40 l S  40 40 m 40 90 l S",
    ]) + b"\n"
    contents = pb.content_stream(pdf, body)
    pages = []
    for angle in (0, 90, 180, 270):
        pages.append(lambda parent, a=angle: pb.page(
            parent, pb.A4, contents, resources, extra=[(b"/Rotate", pb.num(a))]))
    catalog, _, _ = pb.simple_document(pdf, pages)
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: page rotation"))


# ---------------------------------------------------------------------------------------------
# Vector art
# ---------------------------------------------------------------------------------------------

@generator("vector-hairlines.pdf")
def vector_hairlines():
    """A width ladder from 0 (the "thinnest line the device can draw") up to 2 pt, plus a dense
    grid at 0.1 pt.

    This is the file `pageDisplay/lineWeight` (T15) is judged against, and it is the file that
    shows whether a hairline survives being tiled: a 0-width line is one device pixel, so at a
    tile boundary it is the first thing to disappear or to double."""
    pdf = pb.Pdf()
    _, regular, bold = _fonts(pdf)
    resources = _font_resource(regular, bold)

    lines = [_label("vector-hairlines.pdf - line width ladder and 0.1 pt grid")]
    y = 760
    for width in (0, 0.05, 0.1, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0):
        lines.append(b"%s w 150 %s m 520 %s l S" % (pb.num(width), pb.num(y), pb.num(y)))
        lines.append(pb.show_text(60, y - 3, 8, "%.2f pt" % width))
        y -= 22
    lines.append(b"0.1 w")
    for step in range(0, 41):
        x = 60 + step * 12
        lines.append(b"%s 120 m %s 480 l S" % (pb.num(x), pb.num(x)))
    for step in range(0, 31):
        y = 120 + step * 12
        lines.append(b"60 %s m 540 %s l S" % (pb.num(y), pb.num(y)))
    # Diagonals: a horizontal or vertical hairline can land exactly on a pixel boundary, a
    # diagonal one never does, and the two are anti-aliased by different code paths.
    lines.append(b"0 w")
    for step in range(0, 12):
        lines.append(b"60 %d m 540 %d l S" % (120 + step * 30, 480 - step * 30))
    contents = pb.content_stream(pdf, b"\n".join(lines) + b"\n")
    catalog, _, _ = pb.simple_document(
        pdf, [lambda parent: pb.page(parent, pb.A4, contents, resources)])
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: hairline vector art"))


@generator("vector-curves.pdf")
def vector_curves():
    """Beziers, both winding rules, dash patterns, line joins and caps.

    The two stars are the point: the same path filled `f` (non-zero) and `f*` (even-odd) differ
    only in the middle, so a renderer that ignores the rule produces a picture that is obviously
    wrong rather than subtly so."""
    pdf = pb.Pdf()
    _, regular, bold = _fonts(pdf)
    resources = _font_resource(regular, bold)

    # A five-pointed star's ten vertices, precomputed as integers rather than with math.cos --
    # same determinism argument as `jpegenc`'s cosine table, and here it also keeps the file
    # readable.
    outer = [(0, 100), (95, 31), (59, -81), (-59, -81), (-95, 31)]
    inner = [(0, 40), (38, 12), (24, -32), (-24, -32), (-38, 12)]
    points = []
    for index in range(5):
        points.append(outer[index])
        points.append(inner[(index + 2) % 5])

    lines = [_label("vector-curves.pdf - beziers, winding rules, dashes, joins")]
    for offset, (operator, caption) in enumerate(((b"f", "non-zero winding (f)"),
                                                  (b"f*", "even-odd winding (f*)"))):
        cx = 170 + offset * 240
        cy = 640
        path = [b"%s %s m" % (pb.num(cx + points[0][0]), pb.num(cy + points[0][1]))]
        for x, y in points[1:]:
            path.append(b"%s %s l" % (pb.num(cx + x), pb.num(cy + y)))
        path.append(b"h " + operator)
        lines.append(b"0.2 0.35 0.7 rg " + b" ".join(path))
        lines.append(pb.show_text(cx - 70, cy - 130, 9, caption))

    lines.append(b"0 g 1.5 w")
    lines.append(b"60 460 m 180 560 300 360 420 460 c S")
    lines.append(b"60 400 m 160 400 260 480 300 400 c 340 320 440 400 540 400 c S")
    lines.append(pb.show_text(60, 340, 9, "cubic beziers, stroked"))

    for index, dash in enumerate((b"[] 0", b"[6 3] 0", b"[1 4] 0", b"[12 3 3 3] 2")):
        y = 300 - index * 24
        lines.append(b"%s d 2 w 60 %d m 400 %d l S" % (dash, y, y))
        lines.append(b"[] 0 d " + pb.show_text(420, y - 3, 8, "dash " + dash.decode("ascii")))

    for index, (join, cap) in enumerate(((0, 0), (1, 1), (2, 2))):
        x = 80 + index * 150
        lines.append(b"%d j %d J 10 w %d 120 m %d 200 l %d 120 l S" % (join, cap, x, x + 40, x + 80))
    contents = pb.content_stream(pdf, b"\n".join(lines) + b"\n")
    catalog, _, _ = pb.simple_document(
        pdf, [lambda parent: pb.page(parent, pb.A4, contents, resources)])
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: curves and winding rules"))


@generator("shading-patterns.pdf")
def shading_patterns():
    """Axial (type 2), radial (type 3) and function-based (type 1) shadings, each driven by a
    /FunctionType 2 exponential or a /FunctionType 0 sampled function."""
    pdf = pb.Pdf()
    _, regular, bold = _fonts(pdf)

    exponential = pdf.add(pb.dictionary([
        (b"/FunctionType", b"2"), (b"/Domain", pb.array([b"0", b"1"])),
        (b"/C0", pb.array([b"0.95", b"0.35", b"0.10"])),
        (b"/C1", pb.array([b"0.10", b"0.25", b"0.75"])),
        (b"/N", b"1")]))

    # A sampled (type 0) function, because type 2 alone would leave the sampled path -- the one
    # that carries a stream, a /BitsPerSample and a /Size -- untested by the whole corpus.
    samples = bytes()
    for index in range(16):
        samples += bytes([index * 17, 255 - index * 17, 128])
    sampled_data, sampled_filter = pb.flate(samples)
    sampled = pdf.add_stream([
        (b"/FunctionType", b"0"), (b"/Domain", pb.array([b"0", b"1"])),
        (b"/Range", pb.array([b"0", b"1", b"0", b"1", b"0", b"1"])),
        (b"/Size", pb.array([b"16"])), (b"/BitsPerSample", b"8"),
        (b"/Filter", sampled_filter)], sampled_data)

    axial = pdf.add(pb.dictionary([
        (b"/ShadingType", b"2"), (b"/ColorSpace", b"/DeviceRGB"),
        (b"/Coords", pb.array([b"60", b"620", b"540", b"780"])),
        (b"/Function", pb.ref(exponential)),
        (b"/Extend", pb.array([b"true", b"true"]))]))
    radial = pdf.add(pb.dictionary([
        (b"/ShadingType", b"3"), (b"/ColorSpace", b"/DeviceRGB"),
        (b"/Coords", pb.array([b"300", b"430", b"0", b"300", b"430", b"160"])),
        (b"/Function", pb.ref(sampled)),
        (b"/Extend", pb.array([b"false", b"false"]))]))
    function_based = pdf.add(pb.dictionary([
        (b"/ShadingType", b"1"), (b"/ColorSpace", b"/DeviceRGB"),
        (b"/Domain", pb.array([b"0", b"1", b"0", b"1"])),
        (b"/Matrix", pb.array([b"200", b"0", b"0", b"120", b"200", b"120"])),
        (b"/Function", pb.ref(exponential))]))

    content = b"\n".join([
        _label("shading-patterns.pdf - axial, radial, function-based"),
        b"q 60 620 480 160 re W n /Sh0 sh Q",
        b"q 140 270 320 320 re W n /Sh1 sh Q",
        b"q /Sh2 sh Q",
        pb.show_text(60, 600, 9, "type 2 axial, exponential function, both ends extended"),
        pb.show_text(60, 250, 9, "type 3 radial, sampled (type 0) function"),
        pb.show_text(60, 100, 9, "type 1 function-based, mapped by /Matrix"),
    ]) + b"\n"
    contents = pb.content_stream(pdf, content)
    resources = pb.dictionary([
        (b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)), (b"/F2", pb.ref(bold))])),
        (b"/Shading", pb.dictionary([(b"/Sh0", pb.ref(axial)), (b"/Sh1", pb.ref(radial)),
                                     (b"/Sh2", pb.ref(function_based))]))])
    catalog, _, _ = pb.simple_document(
        pdf, [lambda parent: pb.page(parent, pb.A4, contents, resources)])
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: shading patterns"))


@generator("tiling-pattern.pdf")
def tiling_pattern():
    """A type 1 (tiling) pattern, coloured and uncoloured.

    An uncoloured pattern takes its colour from the /Pattern colour space at use time, so the same
    pattern object paints two different colours on one page -- which is the part a renderer that
    caches the pattern's rasterization by object gets wrong."""
    pdf = pb.Pdf()
    _, regular, bold = _fonts(pdf)

    coloured_body = (b"0.85 0.2 0.2 rg 0 0 12 12 re f "
                     b"0.15 0.3 0.65 rg 12 12 12 12 re f "
                     b"0 g 0.4 w 0 12 m 24 12 l S 12 0 m 12 24 l S\n")
    coloured_data, coloured_filter = pb.flate(coloured_body)
    coloured = pdf.add_stream([
        (b"/Type", b"/Pattern"), (b"/PatternType", b"1"), (b"/PaintType", b"1"),
        (b"/TilingType", b"1"), (b"/BBox", pb.rect(0, 0, 24, 24)),
        (b"/XStep", b"24"), (b"/YStep", b"24"),
        (b"/Resources", b"<< >>"), (b"/Filter", coloured_filter)], coloured_data)

    uncoloured_body = b"0 0 m 16 16 l 0 16 m 16 0 l 1 w S\n"
    uncoloured_data, uncoloured_filter = pb.flate(uncoloured_body)
    uncoloured = pdf.add_stream([
        (b"/Type", b"/Pattern"), (b"/PatternType", b"1"), (b"/PaintType", b"2"),
        (b"/TilingType", b"1"), (b"/BBox", pb.rect(0, 0, 16, 16)),
        (b"/XStep", b"16"), (b"/YStep", b"16"),
        (b"/Resources", b"<< >>"), (b"/Filter", uncoloured_filter)], uncoloured_data)

    content = b"\n".join([
        _label("tiling-pattern.pdf - coloured and uncoloured type 1 patterns"),
        b"q /Pattern cs /P0 scn 60 520 480 200 re f Q",
        b"q /Pattern cs /P1 scn 0.8 0.1 0.1 /P1 scn 60 300 220 180 re f Q",
        b"q /Pattern cs 0.1 0.4 0.2 /P1 scn 320 300 220 180 re f Q",
        pb.show_text(60, 500, 9, "PaintType 1: the pattern carries its own colours"),
        pb.show_text(60, 280, 9, "PaintType 2: the same pattern, two colours from the page"),
    ]) + b"\n"
    contents = pb.content_stream(pdf, content)
    resources = pb.dictionary([
        (b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)), (b"/F2", pb.ref(bold))])),
        (b"/Pattern", pb.dictionary([(b"/P0", pb.ref(coloured)), (b"/P1", pb.ref(uncoloured))]))])
    catalog, _, _ = pb.simple_document(
        pdf, [lambda parent: pb.page(parent, pb.A4, contents, resources)])
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: tiling patterns"))


@generator("transparency-groups.pdf")
def transparency_groups():
    """Constant alpha, blend modes, a transparency group XObject and a luminosity soft mask.

    Every one of those is a separate branch in a compositor, and a page that uses only `ca` would
    let three of them ship untested. The page also has a KNOCKOUT-free isolated group, because
    /I and /K are the two flags most often ignored."""
    pdf = pb.Pdf()
    _, regular, bold = _fonts(pdf)

    group_body = (b"0.9 0.2 0.2 rg 0 0 120 120 re f "
                  b"0.2 0.4 0.9 rg 40 40 120 120 re f\n")
    group_data, group_filter = pb.flate(group_body)
    group = pdf.add_stream([
        (b"/Type", b"/XObject"), (b"/Subtype", b"/Form"),
        (b"/BBox", pb.rect(0, 0, 160, 160)),
        (b"/Group", pb.dictionary([(b"/S", b"/Transparency"), (b"/CS", b"/DeviceRGB"),
                                   (b"/I", b"true"), (b"/K", b"false")])),
        (b"/Resources", b"<< >>"), (b"/Filter", group_filter)], group_data)

    mask_body = b"0 g 0 0 200 200 re f 1 g 30 30 140 140 re f\n"
    mask_data, mask_filter = pb.flate(mask_body)
    mask_form = pdf.add_stream([
        (b"/Type", b"/XObject"), (b"/Subtype", b"/Form"),
        (b"/BBox", pb.rect(0, 0, 200, 200)),
        (b"/Group", pb.dictionary([(b"/S", b"/Transparency"), (b"/CS", b"/DeviceGray")])),
        (b"/Resources", b"<< >>"), (b"/Filter", mask_filter)], mask_data)

    states = []
    for index, (name, pairs) in enumerate((
            ("GSalpha", [(b"/ca", b"0.45"), (b"/CA", b"0.45")]),
            ("GSmul", [(b"/BM", b"/Multiply"), (b"/ca", b"1")]),
            ("GSscreen", [(b"/BM", b"/Screen"), (b"/ca", b"1")]),
            ("GSdiff", [(b"/BM", b"/Difference"), (b"/ca", b"1")]),
            ("GSmask", [(b"/SMask", pb.dictionary([(b"/S", b"/Luminosity"),
                                                   (b"/G", pb.ref(mask_form))]))]),
            ("GSnone", [(b"/ca", b"1"), (b"/CA", b"1"), (b"/BM", b"/Normal"),
                        (b"/SMask", b"/None")]))):
        states.append((name, pdf.add(pb.dictionary([(b"/Type", b"/ExtGState")] + pairs))))

    content = [_label("transparency-groups.pdf - alpha, blend modes, groups, soft mask")]
    content.append(b"0.95 0.75 0.2 rg 60 560 200 160 re f")
    content.append(b"q /GSalpha gs 0.2 0.3 0.9 rg 140 600 200 160 re f Q")
    content.append(pb.show_text(60, 545, 9, "constant alpha 0.45 over an opaque rectangle"))

    for index, mode in enumerate(("GSmul", "GSscreen", "GSdiff")):
        x = 60 + index * 165
        content.append(b"0.95 0.55 0.15 rg %d 380 120 120 re f" % x)
        content.append(b"q /%s gs 0.15 0.45 0.85 rg %d 420 120 120 re f Q"
                       % (mode.encode("ascii"), x + 30))
        content.append(pb.show_text(x, 365, 8, mode[2:]))

    content.append(b"q 1 0 0 1 60 150 cm /Fx0 Do Q")
    content.append(b"q /GSmask gs 1 0 0 1 300 130 cm 0.1 0.6 0.3 rg 0 0 200 200 re f Q")
    content.append(pb.show_text(60, 120, 9, "isolated group XObject (left); luminosity soft mask (right)"))

    contents = pb.content_stream(pdf, b"\n".join(content) + b"\n")
    resources = pb.dictionary([
        (b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)), (b"/F2", pb.ref(bold))])),
        (b"/ExtGState", pb.dictionary([(b"/" + name.encode("ascii"), pb.ref(number))
                                       for name, number in states])),
        (b"/XObject", pb.dictionary([(b"/Fx0", pb.ref(group))]))])
    page_extra = [(b"/Group", pb.dictionary([(b"/S", b"/Transparency"), (b"/CS", b"/DeviceRGB")]))]
    catalog, _, _ = pb.simple_document(
        pdf, [lambda parent: pb.page(parent, pb.A4, contents, resources, extra=page_extra)])
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: transparency"))


# ---------------------------------------------------------------------------------------------
# Images
# ---------------------------------------------------------------------------------------------

def _photo_samples(width, height):
    """A synthetic "photograph": smooth gradients plus a checker plus a hard edge. Smooth areas
    exercise the DC path, the checker the AC path, and the hard edge is where a lossy codec's
    ringing shows -- a flat image would make the DCT fixture prove almost nothing."""
    out = []
    for y in range(height):
        for x in range(width):
            red = min(255, x * 255 // max(width - 1, 1))
            green = min(255, y * 255 // max(height - 1, 1))
            blue = 220 if ((x // 8) + (y // 8)) % 2 else 60
            if x > width * 2 // 3:
                red, green, blue = 250, 250, 250
            out += [red, green, blue]
    return out


def _image_page(pdf, title, image_number, caption, width, height):
    _, regular, bold = _fonts(pdf)
    scale = 420.0
    content = b"\n".join([
        _label(title),
        pb.show_text(48, 700, 9, caption),
        b"q %s 0 0 %s 48 %s cm /Im0 Do Q" % (
            pb.num(scale), pb.num(scale * height / width), pb.num(660 - scale * height / width)),
    ]) + b"\n"
    contents = pb.content_stream(pdf, content)
    resources = pb.dictionary([
        (b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)), (b"/F2", pb.ref(bold))])),
        (b"/XObject", pb.dictionary([(b"/Im0", pb.ref(image_number))]))])
    catalog, _, _ = pb.simple_document(
        pdf, [lambda parent: pb.page(parent, pb.A4, contents, resources)])
    return catalog


@generator("image-dct-rgb.pdf")
def image_dct_rgb():
    pdf = pb.Pdf()
    width, height = 96, 72
    samples = _photo_samples(width, height)
    blob = jpegenc.encode(width, height, samples, 3)
    # A wider tolerance than the default, and the reason is the point of the image: the hard
    # white edge at two thirds across rings, so the worst sample is 33 while the median is 1 and
    # the 95th percentile is 5. Ringing at an edge is what a lossy codec DOES -- an image tuned
    # until it round-tripped tightly would be an image with no edge in it.
    jpegenc.selfcheck(width, height, samples, 3, blob, tolerance=40)
    image = pdf.add_stream([
        (b"/Type", b"/XObject"), (b"/Subtype", b"/Image"),
        (b"/Width", pb.num(width)), (b"/Height", pb.num(height)),
        (b"/ColorSpace", b"/DeviceRGB"), (b"/BitsPerComponent", b"8"),
        (b"/Filter", b"/DCTDecode")], blob)
    catalog = _image_page(pdf, "image-dct-rgb.pdf",
                          image, "baseline JPEG, 4:4:4, 3 components, /DCTDecode",
                          width, height)
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: DCT RGB image"))


@generator("image-dct-gray.pdf")
def image_dct_gray():
    pdf = pb.Pdf()
    width, height = 96, 72
    samples = [min(255, x * 2 + y * 2) if x < width * 3 // 4 else 30
               for y in range(height) for x in range(width)]
    blob = jpegenc.encode(width, height, samples, 1)
    jpegenc.selfcheck(width, height, samples, 1, blob)
    image = pdf.add_stream([
        (b"/Type", b"/XObject"), (b"/Subtype", b"/Image"),
        (b"/Width", pb.num(width)), (b"/Height", pb.num(height)),
        (b"/ColorSpace", b"/DeviceGray"), (b"/BitsPerComponent", b"8"),
        (b"/Filter", b"/DCTDecode")], blob)
    catalog = _image_page(pdf, "image-dct-gray.pdf",
                          image, "baseline JPEG, 1 component, /DCTDecode", width, height)
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: DCT grayscale image"))


@generator("image-jpx.pdf")
def image_jpx():
    """JPEG 2000. Read `corpus/jpxenc.py`'s header before judging the rendered result: the
    codestream carries ONE coding pass, so a 0/255 source decodes to roughly 0/128. That is a
    truncated codestream, not a broken one, and it is what makes the /JPXDecode path reachable
    without a full EBCOT encoder in the tree."""
    pdf = pb.Pdf()
    width, height = 64, 64
    samples = []
    for y in range(height):
        for x in range(width):
            ring = 14 * 14 <= (x - 32) ** 2 + (y - 32) ** 2 <= 24 * 24
            bar = 4 <= y < 10 and 8 <= x < 56
            samples.append(0 if (ring or bar) else 255)
    blob = jpxenc.encode(width, height, samples, 1)
    jpxenc.selfcheck(width, height, samples, 1, blob)
    image = pdf.add_stream([
        (b"/Type", b"/XObject"), (b"/Subtype", b"/Image"),
        (b"/Width", pb.num(width)), (b"/Height", pb.num(height)),
        (b"/BitsPerComponent", b"8"), (b"/Filter", b"/JPXDecode")], blob)
    catalog = _image_page(pdf, "image-jpx.pdf",
                          image, "JPEG 2000, one coding pass - see corpus/jpxenc.py",
                          width, height)
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: JPX image"))


def _fax_bitmap(width, height):
    """A page-like bilevel image: horizontal rules, a filled block, and text-shaped speckle. Runs
    of very different lengths are what make a CCITT fixture exercise both terminating and make-up
    codes -- a checkerboard would only ever produce short runs."""
    bits = []
    for y in range(height):
        for x in range(width):
            rule = 1 if y in (6, 7, height - 8, height - 7) else 0
            block = 1 if (18 <= x < 90 and 20 <= y < 34) else 0
            speckle = 1 if (40 <= y < 96 and (x // 3 + y * 2) % 11 < 2) else 0
            long_run = 1 if (100 <= y < 108) else 0
            bits.append(1 if (rule or block or speckle or long_run) else 0)
    return bits


@generator("image-ccitt-g4.pdf")
def image_ccitt_g4():
    pdf = pb.Pdf()
    width, height = 128, 120
    bits = _fax_bitmap(width, height)
    blob = ccitt.encode(bits, width, height, k=-1)
    image = pdf.add_stream([
        (b"/Type", b"/XObject"), (b"/Subtype", b"/Image"),
        (b"/Width", pb.num(width)), (b"/Height", pb.num(height)),
        (b"/ColorSpace", b"/DeviceGray"), (b"/BitsPerComponent", b"1"),
        (b"/Filter", b"/CCITTFaxDecode"),
        (b"/DecodeParms", pb.dictionary([(b"/K", b"-1"), (b"/Columns", pb.num(width)),
                                         (b"/Rows", pb.num(height)),
                                         (b"/BlackIs1", b"true")]))], blob)
    catalog = _image_page(pdf, "image-ccitt-g4.pdf",
                          image, "CCITT Group 4 (T.6), K=-1, /BlackIs1 true", width, height)
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: CCITT G4 image"))


@generator("image-ccitt-g3.pdf")
def image_ccitt_g3():
    pdf = pb.Pdf()
    width, height = 128, 120
    bits = _fax_bitmap(width, height)
    blob = ccitt.encode(bits, width, height, k=0)
    image = pdf.add_stream([
        (b"/Type", b"/XObject"), (b"/Subtype", b"/Image"),
        (b"/Width", pb.num(width)), (b"/Height", pb.num(height)),
        (b"/ColorSpace", b"/DeviceGray"), (b"/BitsPerComponent", b"1"),
        (b"/Filter", b"/CCITTFaxDecode"),
        (b"/DecodeParms", pb.dictionary([(b"/K", b"0"), (b"/Columns", pb.num(width)),
                                         (b"/Rows", pb.num(height)),
                                         (b"/BlackIs1", b"true")]))], blob)
    catalog = _image_page(pdf, "image-ccitt-g3.pdf",
                          image, "CCITT Group 3 one-dimensional (T.4), K=0", width, height)
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: CCITT G3 image"))


@generator("image-flate-predictor.pdf")
def image_flate_predictor():
    """/FlateDecode with a PNG Up predictor. A decoder that ignores /DecodeParms produces an
    image that is wrong in a specific, obvious way -- each row offset from the one above -- rather
    than failing, which is exactly why the predictor needs its own fixture."""
    pdf = pb.Pdf()
    width, height = 120, 90
    raw = bytes(_photo_samples(width, height))
    predicted = pb.png_predict(raw, width, 3, 8, 12)
    data, filter_name = pb.flate(predicted, "dynamic")
    image = pdf.add_stream([
        (b"/Type", b"/XObject"), (b"/Subtype", b"/Image"),
        (b"/Width", pb.num(width)), (b"/Height", pb.num(height)),
        (b"/ColorSpace", b"/DeviceRGB"), (b"/BitsPerComponent", b"8"),
        (b"/Filter", filter_name),
        (b"/DecodeParms", pb.dictionary([(b"/Predictor", b"12"), (b"/Colors", b"3"),
                                         (b"/BitsPerComponent", b"8"),
                                         (b"/Columns", pb.num(width))]))], data)
    catalog = _image_page(pdf, "image-flate-predictor.pdf",
                          image, "/FlateDecode with PNG Up predictor, dynamic Huffman block",
                          width, height)
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: predicted Flate image"))


@generator("image-indexed-stencil.pdf")
def image_indexed_stencil():
    """Three colour models on one page: an /Indexed palette image, a 1-bit /ImageMask stencil
    painted in the current colour, and an 8-bit /SMask alpha channel."""
    pdf = pb.Pdf()
    _, regular, bold = _fonts(pdf)

    palette = bytes([255, 255, 255, 220, 40, 40, 40, 90, 200, 250, 200, 60])
    indices = bytearray()
    for y in range(64):
        for x in range(64):
            indices.append(((x // 8) + (y // 8)) % 4)
    indexed_data, indexed_filter = pb.flate(bytes(indices))
    indexed = pdf.add_stream([
        (b"/Type", b"/XObject"), (b"/Subtype", b"/Image"),
        (b"/Width", b"64"), (b"/Height", b"64"), (b"/BitsPerComponent", b"8"),
        (b"/ColorSpace", pb.array([b"/Indexed", b"/DeviceRGB", b"3",
                                   pb.hexstring(palette)])),
        (b"/Filter", indexed_filter)], indexed_data)

    stencil_rows = bytearray()
    for y in range(48):
        row = 0
        for x in range(48):
            on = 1 if (abs(x - 24) + abs(y - 24) < 18) else 0
            row = (row << 1) | on
        stencil_rows += row.to_bytes(6, "big")
    stencil_data, stencil_filter = pb.flate(bytes(stencil_rows))
    stencil = pdf.add_stream([
        (b"/Type", b"/XObject"), (b"/Subtype", b"/Image"),
        (b"/Width", b"48"), (b"/Height", b"48"), (b"/ImageMask", b"true"),
        (b"/Decode", pb.array([b"1", b"0"])),
        (b"/Filter", stencil_filter)], stencil_data)

    alpha = bytes(min(255, x * 4) for _ in range(48) for x in range(64))
    alpha_data, alpha_filter = pb.flate(alpha)
    smask = pdf.add_stream([
        (b"/Type", b"/XObject"), (b"/Subtype", b"/Image"),
        (b"/Width", b"64"), (b"/Height", b"48"), (b"/ColorSpace", b"/DeviceGray"),
        (b"/BitsPerComponent", b"8"), (b"/Filter", alpha_filter)], alpha_data)

    masked_rgb = bytes(value for _ in range(48) for x in range(64)
                       for value in (40, 90, 200))
    masked_data, masked_filter = pb.flate(masked_rgb)
    masked = pdf.add_stream([
        (b"/Type", b"/XObject"), (b"/Subtype", b"/Image"),
        (b"/Width", b"64"), (b"/Height", b"48"), (b"/ColorSpace", b"/DeviceRGB"),
        (b"/BitsPerComponent", b"8"), (b"/SMask", pb.ref(smask)),
        (b"/Filter", masked_filter)], masked_data)

    content = b"\n".join([
        _label("image-indexed-stencil.pdf - indexed palette, stencil mask, soft mask"),
        b"q 200 0 0 200 48 520 cm /Im0 Do Q",
        b"q 0.85 0.25 0.15 rg 160 0 0 160 300 540 cm /Im1 Do Q",
        b"q 0.1 0.1 0.1 rg 48 300 480 40 re f Q",
        b"q 320 0 0 240 48 250 cm /Im2 Do Q",
        pb.show_text(48, 500, 9, "left: /Indexed 4-entry palette   right: /ImageMask stencil"),
        pb.show_text(48, 230, 9, "below: 8-bit /SMask alpha over a dark bar"),
    ]) + b"\n"
    contents = pb.content_stream(pdf, content)
    resources = pb.dictionary([
        (b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)), (b"/F2", pb.ref(bold))])),
        (b"/XObject", pb.dictionary([(b"/Im0", pb.ref(indexed)), (b"/Im1", pb.ref(stencil)),
                                     (b"/Im2", pb.ref(masked))]))])
    catalog, _, _ = pb.simple_document(
        pdf, [lambda parent: pb.page(parent, pb.A4, contents, resources)])
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: indexed and masked images"))


@generator("image-lzw-ascii85.pdf")
def image_lzw_ascii85():
    """A filter CHAIN -- /ASCII85Decode then /LZWDecode -- and a second image through
    /RunLengthDecode.

    The chain matters more than either filter: /Filter is an ARRAY applied in order, and a reader
    that handles one filter per stream passes every single-filter fixture in this corpus."""
    pdf = pb.Pdf()
    _, regular, bold = _fonts(pdf)

    width, height = 80, 60
    raw = bytes(value for y in range(height) for x in range(width)
                for value in (min(255, x * 3), min(255, y * 4), 128))
    lzw_data, _ = pb.lzw(raw)
    chained, _ = pb.ascii85(lzw_data)
    chain_image = pdf.add_stream([
        (b"/Type", b"/XObject"), (b"/Subtype", b"/Image"),
        (b"/Width", pb.num(width)), (b"/Height", pb.num(height)),
        (b"/ColorSpace", b"/DeviceRGB"), (b"/BitsPerComponent", b"8"),
        (b"/Filter", pb.array([b"/ASCII85Decode", b"/LZWDecode"]))], chained)

    flat = bytes(value for y in range(height) for x in range(width)
                 for value in ((230, 80, 40) if (y // 10) % 2 else (30, 60, 160)))
    rle_data, rle_filter = pb.runlength(flat)
    rle_image = pdf.add_stream([
        (b"/Type", b"/XObject"), (b"/Subtype", b"/Image"),
        (b"/Width", pb.num(width)), (b"/Height", pb.num(height)),
        (b"/ColorSpace", b"/DeviceRGB"), (b"/BitsPerComponent", b"8"),
        (b"/Filter", rle_filter)], rle_data)

    hex_text = b"BT /F1 11 Tf 1 0 0 1 48 240 Tm (this content stream arrived through ASCIIHexDecode) Tj ET\n"
    hex_data, hex_filter = pb.asciihex(hex_text)
    hex_stream = pdf.add_stream([(b"/Filter", hex_filter)], hex_data)

    content = b"\n".join([
        _label("image-lzw-ascii85.pdf - filter chains"),
        b"q 300 0 0 225 48 480 cm /Im0 Do Q",
        b"q 200 0 0 150 380 480 cm /Im1 Do Q",
        pb.show_text(48, 460, 9, "left: [/ASCII85Decode /LZWDecode]   right: /RunLengthDecode"),
    ]) + b"\n"
    contents = pb.content_stream(pdf, content)
    resources = pb.dictionary([
        (b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)), (b"/F2", pb.ref(bold))])),
        (b"/XObject", pb.dictionary([(b"/Im0", pb.ref(chain_image)),
                                     (b"/Im1", pb.ref(rle_image))]))])
    # Two content streams in an array: the second arrives through a different filter, and a
    # /Contents array is itself a structure a reader has to handle.
    catalog, _, _ = pb.simple_document(pdf, [lambda parent: pb.dictionary([
        (b"/Type", b"/Page"), (b"/Parent", pb.ref(parent)),
        (b"/MediaBox", pb.rect(0, 0, pb.A4[0], pb.A4[1])),
        (b"/Resources", resources),
        (b"/Contents", pb.array([pb.ref(contents), pb.ref(hex_stream)]))])])
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: filter chains"))


# ---------------------------------------------------------------------------------------------
# Embedded fonts
# ---------------------------------------------------------------------------------------------

@generator("cjk-embedded.pdf")
def cjk_embedded():
    """An embedded CJK subset as CIDFontType2 with Identity-H.

    Identity-H means the string bytes ARE glyph indices, two bytes each -- so this file tests the
    path that has no encoding lookup at all, which is the one PDF producers actually emit for CJK.
    The glyphs are nine real hanzi drawn from rectangles; see `corpus/fontenc.py` for what that
    does and does not demonstrate."""
    pdf = pb.Pdf()
    font_bytes, mapping, widths = fontenc.cjk_font()
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")

    font_data, font_filter = pb.flate(font_bytes)
    font_file = pdf.add_stream([(b"/Length1", pb.num(len(font_bytes))),
                                (b"/Filter", font_filter)], font_data)
    descriptor = pdf.add(pb.dictionary([
        (b"/Type", b"/FontDescriptor"), (b"/FontName", pb.name("RawPageCorpusCJK")),
        (b"/Flags", b"4"), (b"/FontBBox", pb.rect(0, -120, 1000, 880)),
        (b"/ItalicAngle", b"0"), (b"/Ascent", b"880"), (b"/Descent", b"-120"),
        (b"/CapHeight", b"700"), (b"/StemV", b"80"), (b"/FontFile2", pb.ref(font_file))]))

    glyph_ids = [mapping[ord(character)] for character in "一二三十口日田中目"]
    width_array = pb.array([b"1", pb.array([pb.num(widths[index])
                                            for index in range(1, len(widths))])])
    cid_font = pdf.add(pb.dictionary([
        (b"/Type", b"/Font"), (b"/Subtype", b"/CIDFontType2"),
        (b"/BaseFont", pb.name("RawPageCorpusCJK")),
        (b"/CIDSystemInfo", pb.dictionary([(b"/Registry", pb.literal(b"Adobe")),
                                           (b"/Ordering", pb.literal(b"Identity")),
                                           (b"/Supplement", b"0")])),
        (b"/FontDescriptor", pb.ref(descriptor)),
        (b"/DW", b"1000"), (b"/W", width_array),
        (b"/CIDToGIDMap", b"/Identity")]))
    composite = pdf.add(pb.dictionary([
        (b"/Type", b"/Font"), (b"/Subtype", b"/Type0"),
        (b"/BaseFont", pb.name("RawPageCorpusCJK")),
        (b"/Encoding", b"/Identity-H"), (b"/DescendantFonts", pb.array([pb.ref(cid_font)]))]))

    def run(y, size, ids):
        codes = b"".join(b"%04X" % gid for gid in ids)
        return b"BT /F3 %s Tf 1 0 0 1 48 %s Tm <%s> Tj ET" % (pb.num(size), pb.num(y), codes)

    content = b"\n".join([
        _label("cjk-embedded.pdf - CIDFontType2, Identity-H, /FontFile2"),
        pb.show_text(48, 720, 9, "nine hanzi from an embedded subset; glyph ids are the string bytes"),
        run(640, 48, glyph_ids),
        run(560, 28, glyph_ids),
        run(500, 14, glyph_ids * 3),
        run(460, 9, glyph_ids * 5),
        pb.show_text(48, 400, 9, "the same nine at 48, 28, 14 and 9 pt -- glyph cache pressure"),
    ]) + b"\n"
    contents = pb.content_stream(pdf, content)
    resources = pb.dictionary([
        (b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)), (b"/F2", pb.ref(bold)),
                                  (b"/F3", pb.ref(composite))]))])
    catalog, _, _ = pb.simple_document(
        pdf, [lambda parent: pb.page(parent, pb.A4, contents, resources)])
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: embedded CJK subset"))


@generator("latin-embedded.pdf")
def latin_embedded():
    """A simple (non-composite) /TrueType font with /FontFile2 and a /Differences encoding.

    This is the control for `cjk-embedded.pdf`: same embedding mechanism, single-byte codes and a
    real encoding table, so a failure in one and not the other localizes immediately."""
    pdf = pb.Pdf()
    font_bytes, mapping, widths = fontenc.latin_font()
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")

    font_data, font_filter = pb.flate(font_bytes)
    font_file = pdf.add_stream([(b"/Length1", pb.num(len(font_bytes))),
                                (b"/Filter", font_filter)], font_data)
    descriptor = pdf.add(pb.dictionary([
        (b"/Type", b"/FontDescriptor"), (b"/FontName", pb.name("RawPageCorpusLatin")),
        (b"/Flags", b"32"), (b"/FontBBox", pb.rect(0, -120, 700, 700)),
        (b"/ItalicAngle", b"0"), (b"/Ascent", b"880"), (b"/Descent", b"-120"),
        (b"/CapHeight", b"700"), (b"/StemV", b"80"), (b"/FontFile2", pb.ref(font_file))]))

    first = min(mapping)
    last = max(mapping)
    widths_by_code = []
    for code in range(first, last + 1):
        widths_by_code.append(pb.num(widths[mapping[code]]) if code in mapping else b"0")
    embedded = pdf.add(pb.dictionary([
        (b"/Type", b"/Font"), (b"/Subtype", b"/TrueType"),
        (b"/BaseFont", pb.name("RawPageCorpusLatin")),
        (b"/FirstChar", pb.num(first)), (b"/LastChar", pb.num(last)),
        (b"/Widths", pb.array(widths_by_code)),
        (b"/Encoding", pb.dictionary([(b"/Type", b"/Encoding"),
                                      (b"/BaseEncoding", b"/WinAnsiEncoding")])),
        (b"/FontDescriptor", pb.ref(descriptor))]))

    content = b"\n".join([
        _label("latin-embedded.pdf - simple /TrueType with /FontFile2"),
        pb.show_text(48, 700, 9, "seven rectilinear capitals from an embedded subset"),
        pb.show_text(48, 620, 42, "LIFT THE FILE", b"/F3"),
        pb.show_text(48, 560, 24, "THE FIFTH TITLE", b"/F3"),
        pb.show_text(48, 520, 11, "LITTLE HILLTOP", b"/F3"),
        pb.show_text(48, 460, 9, "and the same text in base-14 Helvetica for comparison:"),
        pb.show_text(48, 430, 42, "LIFT THE FILE"),
    ]) + b"\n"
    contents = pb.content_stream(pdf, content)
    resources = pb.dictionary([
        (b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)), (b"/F2", pb.ref(bold)),
                                  (b"/F3", pb.ref(embedded))]))])
    catalog, _, _ = pb.simple_document(
        pdf, [lambda parent: pb.page(parent, pb.A4, contents, resources)])
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: embedded Latin subset"))


def _absent_widths(first, last):
    """A plausible, NON-UNIFORM width for each code.

    The widths must vary, and finding that out cost a debugging session at S2 · T16: with every
    /Widths entry equal, PDFium concludes the font is MONOSPACED and sets `FXFONT_FF_FIXEDPITCH` in
    the `pitch_family` it passes to the font hook — so both of this file's fonts were resolved down
    the fixed-pitch branch and both came back as Consolas, which made the serif bit in /Flags
    unobservable. The engine is right to infer it; the fixture was wrong to invite it.

    The shape below is a crude Latin metric — narrow for i/l/j and punctuation, wide for m/w/M/W —
    computed rather than tabulated so that it stays reproducible byte for byte."""
    narrow = set(b"iljtfI.,;:'|!()[]{}")
    wide = set(b"mwMW@")
    widths = []
    for code in range(first, last + 1):
        if code in narrow:
            widths.append(278)
        elif code in wide:
            widths.append(889)
        elif chr(code).isupper():
            widths.append(667)
        else:
            widths.append(500)
    return widths


def _absent_font(pdf, ps_name, flags):
    """A font the document NAMES and does not embed: a /FontDescriptor with no /FontFile of any
    kind. That is not damage -- it is what every producer emits when the author's font was not
    licensed for embedding, and it is the single most common reason a PDF looks different on two
    machines.

    The /BaseFont carries a six-capital subset tag even though nothing is subset, because real
    files do: a producer that subsets and then fails to embed leaves the tag behind. It is here so
    that `core::render::normalizeBaseFontName` is exercised end to end rather than only by its own
    unit case."""
    descriptor = pdf.add(pb.dictionary([
        (b"/Type", b"/FontDescriptor"), (b"/FontName", pb.name(ps_name)),
        (b"/Flags", pb.num(flags)), (b"/FontBBox", pb.rect(-200, -250, 1000, 900)),
        (b"/ItalicAngle", b"0"), (b"/Ascent", b"880"), (b"/Descent", b"-250"),
        (b"/CapHeight", b"700"), (b"/StemV", b"80"), (b"/MissingWidth", b"500")]))
    first, last = 32, 126
    return pdf.add(pb.dictionary([
        (b"/Type", b"/Font"), (b"/Subtype", b"/TrueType"),
        (b"/BaseFont", pb.name(ps_name)),
        (b"/FirstChar", pb.num(first)), (b"/LastChar", pb.num(last)),
        (b"/Widths", pb.array([pb.num(w) for w in _absent_widths(first, last)])),
        (b"/Encoding", pb.dictionary([(b"/Type", b"/Encoding"),
                                      (b"/BaseEncoding", b"/WinAnsiEncoding")])),
        (b"/FontDescriptor", pb.ref(descriptor))]))


@generator("font-not-embedded.pdf")
def font_not_embedded():
    """Two fonts the document names and does not embed -- one serif, one sans -- so that whatever
    renders them had to CHOOSE something.

    THIS IS THE FILE GUIDE CASE A-66 NEEDS, and it exists because none of the other 29 could serve.
    Every other text fixture uses either a base-14 face -- which every conforming reader is obliged
    to have, so PDFium answers from its own bundled Foxit copies and no substitution happens at all
    -- or an embedded /FontFile2, where the outlines are in the file. A missing font is a third
    thing, and it is the only one that reaches a font resolver.

    The two /BaseFont names are deliberately absent from every machine on earth, so the substitute
    is never accidentally the requested face: `RawPageAbsentSerif` and `RawPageAbsentSans` are ours
    and are not installed anywhere. The /Flags differ in exactly one bit -- serif (2) -- which is
    the input `core::render::substitutionCandidates` branches on, so a resolver that ignored the
    descriptor and always answered the same family produces a visibly identical pair of lines and
    fails the case.
    """
    pdf = pb.Pdf()
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")

    # 34 = serif (2) + nonsymbolic (32); 32 = nonsymbolic alone.
    serif = _absent_font(pdf, "RAWPGA+RawPageAbsentSerif", 34)
    sans = _absent_font(pdf, "RAWPGB+RawPageAbsentSans", 32)

    content = b"\n".join([
        _label("font-not-embedded.pdf - two fonts named, neither embedded"),
        pb.show_text(48, 700, 9, "the two lines below name fonts this file does not carry:"),
        pb.show_text(48, 650, 28, "Serif substitution sample", b"/F3"),
        pb.show_text(48, 600, 28, "Sans substitution sample", b"/F4"),
        pb.show_text(48, 540, 9, "and the same text in base-14 Helvetica, which is never substituted:"),
        pb.show_text(48, 500, 28, "Serif substitution sample"),
    ]) + b"\n"
    contents = pb.content_stream(pdf, content)
    resources = pb.dictionary([
        (b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)), (b"/F2", pb.ref(bold)),
                                  (b"/F3", pb.ref(serif)), (b"/F4", pb.ref(sans))]))])
    catalog, _, _ = pb.simple_document(
        pdf, [lambda parent: pb.page(parent, pb.A4, contents, resources)])
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: fonts named, not embedded"))


# ---------------------------------------------------------------------------------------------
# Interactive structure
# ---------------------------------------------------------------------------------------------

@generator("forms-acroform.pdf")
def forms_acroform():
    """The form-heavy page D2-11 asks for: text fields, a multiline field, a password field, two
    checkboxes, a radio group, a combo box, a list box and a push button -- every one with a real
    appearance stream, because a widget without /AP is a widget that renders as nothing and would
    make the fixture look correct while proving the opposite."""
    pdf = pb.Pdf()
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    zapf = pdf.add(pb.dictionary([(b"/Type", b"/Font"), (b"/Subtype", b"/Type1"),
                                 (b"/BaseFont", b"/ZapfDingbats")]))
    dr = pb.dictionary([(b"/Font", pb.dictionary([(b"/Helv", pb.ref(regular)),
                                                  (b"/ZaDb", pb.ref(zapf))]))])

    page_number = pdf.reserve()
    pages_number = pdf.reserve()
    widgets = []

    def appearance(width, height, body):
        data, filter_name = pb.flate(body)
        return pdf.add_stream([
            (b"/Type", b"/XObject"), (b"/Subtype", b"/Form"),
            (b"/BBox", pb.rect(0, 0, width, height)),
            (b"/Resources", dr), (b"/Filter", filter_name)], data)

    def field(name, x, y, width, height, pairs, body):
        stream = appearance(width, height, body)
        number = pdf.add(pb.dictionary([
            (b"/Type", b"/Annot"), (b"/Subtype", b"/Widget"),
            (b"/Rect", pb.rect(x, y, x + width, y + height)),
            (b"/T", pb.utf16(name)), (b"/F", b"4"),
            (b"/P", pb.ref(page_number)),
            (b"/DA", pb.literal(b"/Helv 10 Tf 0 g")),
            (b"/MK", pb.dictionary([(b"/BC", pb.array([b"0.3"])),
                                    (b"/BG", pb.array([b"0.94"]))])),
            (b"/AP", pb.dictionary([(b"/N", pb.ref(stream))]))] + pairs))
        widgets.append(number)
        return number

    border = b"0.94 g 0 0 %s %s re f 0.3 G 0.5 w 0.25 0.25 %s %s re S\n"

    def text_body(width, height, text, size=10):
        return (border % (pb.num(width), pb.num(height), pb.num(width - 0.5),
                          pb.num(height - 0.5))
                + b"/Tx BMC q BT /Helv %s Tf 0 g 1 0 0 1 3 %s Tm %s Tj ET Q EMC\n"
                % (pb.num(size), pb.num(height / 2 - size * 0.36), pb.literal(text)))

    field("name", 160, 700, 260, 22, [(b"/FT", b"/Tx"), (b"/V", pb.utf16("Ada Lovelace"))],
          text_body(260, 22, "Ada Lovelace"))
    field("notes", 160, 610, 380, 76,
          [(b"/FT", b"/Tx"), (b"/Ff", b"4096"), (b"/V", pb.utf16("multiline field"))],
          text_body(380, 76, "multiline field", 10))
    field("secret", 160, 570, 200, 22,
          [(b"/FT", b"/Tx"), (b"/Ff", b"8192"), (b"/V", pb.utf16("hidden"))],
          text_body(200, 22, "******"))

    check_on = appearance(16, 16, b"0.94 g 0 0 16 16 re f 0 G 0.7 w 0.35 0.35 15.3 15.3 re S "
                                  b"BT /ZaDb 12 Tf 0 g 1 0 0 1 3 3 Tm (4) Tj ET\n")
    check_off = appearance(16, 16, b"0.94 g 0 0 16 16 re f 0 G 0.7 w 0.35 0.35 15.3 15.3 re S\n")
    for index, (label, value) in enumerate((("agree", b"/Yes"), ("subscribe", b"/Off"))):
        number = pdf.add(pb.dictionary([
            (b"/Type", b"/Annot"), (b"/Subtype", b"/Widget"), (b"/FT", b"/Btn"),
            (b"/Rect", pb.rect(160 + index * 140, 530, 176 + index * 140, 546)),
            (b"/T", pb.utf16(label)), (b"/F", b"4"), (b"/P", pb.ref(page_number)),
            (b"/V", value), (b"/AS", value),
            (b"/DA", pb.literal(b"/ZaDb 0 Tf 0 g")),
            (b"/AP", pb.dictionary([(b"/N", pb.dictionary([
                (b"/Yes", pb.ref(check_on)), (b"/Off", pb.ref(check_off))]))]))]))
        widgets.append(number)

    radio_kids = []
    radio_parent = pdf.reserve()
    for index, export in enumerate((b"/A", b"/B", b"/C")):
        on = appearance(16, 16, b"0.94 g 8 8 m 8 8 7 0 360 arc f 0 G 0.7 w "
                                b"0.35 0.35 15.3 15.3 re S BT /ZaDb 12 Tf 0 g "
                                b"1 0 0 1 3 3 Tm (l) Tj ET\n")
        off = appearance(16, 16, b"0.94 g 0 0 16 16 re f 0 G 0.7 w 0.35 0.35 15.3 15.3 re S\n")
        number = pdf.add(pb.dictionary([
            (b"/Type", b"/Annot"), (b"/Subtype", b"/Widget"),
            (b"/Rect", pb.rect(160 + index * 60, 490, 176 + index * 60, 506)),
            (b"/F", b"4"), (b"/P", pb.ref(page_number)),
            (b"/Parent", pb.ref(radio_parent)),
            (b"/AS", export if index == 0 else b"/Off"),
            (b"/AP", pb.dictionary([(b"/N", pb.dictionary([
                (export, pb.ref(on)), (b"/Off", pb.ref(off))]))]))]))
        radio_kids.append(number)
        widgets.append(number)
    pdf.set(radio_parent, pb.dictionary([
        (b"/FT", b"/Btn"), (b"/Ff", b"32768"), (b"/T", pb.utf16("choice")),
        (b"/V", b"/A"), (b"/Kids", pb.array([pb.ref(kid) for kid in radio_kids]))]))

    field("country", 160, 450, 200, 22,
          [(b"/FT", b"/Ch"), (b"/Ff", b"131072"), (b"/V", pb.utf16("Scotland")),
           (b"/Opt", pb.array([pb.utf16("Scotland"), pb.utf16("Taiwan"),
                               pb.utf16("Iceland")]))],
          text_body(200, 22, "Scotland"))
    field("tags", 160, 350, 200, 84,
          [(b"/FT", b"/Ch"), (b"/V", pb.utf16("render")),
           (b"/Opt", pb.array([pb.utf16("render"), pb.utf16("parse"), pb.utf16("cache")]))],
          border % (b"200", b"84", b"199.5", b"83.5")
          + b"0.75 0.85 1 rg 1 63 198 20 re f\n"
          + b"BT /Helv 10 Tf 0 g 1 0 0 1 4 69 Tm (render) Tj 0 -20 Td (parse) Tj "
            b"0 -20 Td (cache) Tj ET\n")

    button = appearance(120, 26, b"0.25 0.4 0.75 rg 0 0 120 26 re f "
                                 b"BT /Helv 11 Tf 1 g 1 0 0 1 24 9 Tm (Submit) Tj ET\n")
    widgets.append(pdf.add(pb.dictionary([
        (b"/Type", b"/Annot"), (b"/Subtype", b"/Widget"), (b"/FT", b"/Btn"),
        (b"/Ff", b"65536"), (b"/T", pb.utf16("submit")),
        (b"/Rect", pb.rect(160, 300, 280, 326)), (b"/F", b"4"),
        (b"/P", pb.ref(page_number)),
        (b"/AP", pb.dictionary([(b"/N", pb.ref(button))]))])))

    labels = [_label("forms-acroform.pdf - AcroForm widgets with appearance streams")]
    for text, y in (("single-line text", 706), ("multiline text", 676), ("password", 576),
                    ("check boxes", 534), ("radio group", 494), ("combo box", 456),
                    ("list box", 420), ("push button", 308)):
        labels.append(pb.show_text(48, y, 9, text))
    contents = pb.content_stream(pdf, b"\n".join(labels) + b"\n")

    pdf.set(page_number, pb.dictionary([
        (b"/Type", b"/Page"), (b"/Parent", pb.ref(pages_number)),
        (b"/MediaBox", pb.rect(0, 0, pb.A4[0], pb.A4[1])),
        (b"/Resources", pb.dictionary([
            (b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)), (b"/F2", pb.ref(bold))]))])),
        (b"/Contents", pb.ref(contents)),
        (b"/Annots", pb.array([pb.ref(number) for number in widgets]))]))
    pdf.set(pages_number, pb.dictionary([
        (b"/Type", b"/Pages"), (b"/Kids", pb.array([pb.ref(page_number)])), (b"/Count", b"1")]))

    fields = [number for number in widgets if number not in radio_kids] + [radio_parent]
    acroform = pb.dictionary([
        (b"/Fields", pb.array([pb.ref(number) for number in fields])),
        (b"/DR", dr), (b"/DA", pb.literal(b"/Helv 10 Tf 0 g")),
        (b"/NeedAppearances", b"false")])
    catalog = pdf.add(pb.dictionary([
        (b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number)), (b"/AcroForm", acroform)]))
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: AcroForm widgets"))


@generator("annots-mixed.pdf")
def annots_mixed():
    """Link, text note, square, circle, line, highlight, underline, strikeout and free text --
    each with an appearance stream, and the link with a /Dest into page 2.

    Stage 6 owns annotations; this file exists at Stage 2 because a page that renders its content
    stream and silently drops /Annots looks correct, and the only way to notice is a fixture whose
    annotations are visible."""
    pdf = pb.Pdf()
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    resources = _font_resource(regular, bold)

    pages_number = pdf.reserve()
    page_one = pdf.reserve()
    page_two = pdf.reserve()

    def appearance(x0, y0, x1, y1, body):
        data, filter_name = pb.flate(body)
        return pdf.add_stream([
            (b"/Type", b"/XObject"), (b"/Subtype", b"/Form"),
            (b"/BBox", pb.rect(0, 0, x1 - x0, y1 - y0)),
            (b"/Resources", resources), (b"/Filter", filter_name)], data)

    annots = []

    def annot(subtype, x0, y0, x1, y1, pairs, body=None):
        entries = [(b"/Type", b"/Annot"), (b"/Subtype", subtype),
                   (b"/Rect", pb.rect(x0, y0, x1, y1)), (b"/F", b"4"),
                   (b"/P", pb.ref(page_one))] + pairs
        if body is not None:
            entries.append((b"/AP", pb.dictionary([(b"/N", pb.ref(
                appearance(x0, y0, x1, y1, body)))])))
        annots.append(pdf.add(pb.dictionary(entries)))

    annot(b"/Link", 48, 736, 260, 754,
          [(b"/Border", pb.array([b"0", b"0", b"1"])),
           (b"/C", pb.array([b"0", b"0", b"1"])),
           (b"/Dest", pb.array([pb.ref(page_two), b"/Fit"]))])
    annot(b"/Text", 500, 720, 520, 740,
          [(b"/Contents", pb.utf16("a sticky note")), (b"/Name", b"/Comment"),
           (b"/T", pb.utf16("corpus"))],
          b"1 0.85 0.3 rg 0 0 20 20 re f 0 G 0.7 w 0.35 0.35 19.3 19.3 re S\n")
    annot(b"/Square", 48, 600, 220, 700,
          [(b"/C", pb.array([b"0.8", b"0.2", b"0.2"])),
           (b"/IC", pb.array([b"0.98", b"0.9", b"0.9"])), (b"/CA", b"0.8")],
          b"0.98 0.9 0.9 rg 0 0 172 100 re f 0.8 0.2 0.2 RG 2 w 1 1 170 98 re S\n")
    annot(b"/Circle", 260, 600, 400, 700,
          [(b"/C", pb.array([b"0.2", b"0.4", b"0.8"])), (b"/CA", b"0.6")],
          b"0.2 0.4 0.8 RG 2 w 70 50 m 70 50 68 0 360 arc S\n")
    annot(b"/Line", 48, 540, 400, 580,
          [(b"/L", pb.array([b"48", b"550", b"400", b"570"])),
           (b"/C", pb.array([b"0.1", b"0.5", b"0.2"]))],
          b"0.1 0.5 0.2 RG 1.5 w 0 10 m 352 30 l S\n")
    annot(b"/Highlight", 48, 480, 300, 500,
          [(b"/C", pb.array([b"1", b"0.95", b"0.3"])),
           (b"/QuadPoints", pb.array([b"48", b"500", b"300", b"500",
                                      b"48", b"480", b"300", b"480"]))],
          b"1 0.95 0.3 rg 0 0 252 20 re f\n")
    annot(b"/Underline", 48, 440, 300, 458,
          [(b"/C", pb.array([b"0.1", b"0.3", b"0.9"])),
           (b"/QuadPoints", pb.array([b"48", b"458", b"300", b"458",
                                      b"48", b"440", b"300", b"440"]))],
          b"0.1 0.3 0.9 RG 1 w 0 2 m 252 2 l S\n")
    annot(b"/StrikeOut", 48, 400, 300, 418,
          [(b"/C", pb.array([b"0.8", b"0.1", b"0.1"])),
           (b"/QuadPoints", pb.array([b"48", b"418", b"300", b"418",
                                      b"48", b"400", b"300", b"400"]))],
          b"0.8 0.1 0.1 RG 1 w 0 9 m 252 9 l S\n")
    annot(b"/FreeText", 340, 400, 540, 470,
          [(b"/Contents", pb.utf16("free text annotation")),
           (b"/DA", pb.literal(b"/F1 10 Tf 0 g"))],
          b"1 1 0.92 rg 0 0 200 70 re f 0.4 G 0.5 w 0.25 0.25 199.5 69.5 re S "
          b"BT /F1 10 Tf 0 g 1 0 0 1 8 48 Tm (free text annotation) Tj ET\n")

    first_content = b"\n".join([
        _label("annots-mixed.pdf - nine annotation types with appearance streams"),
        pb.show_text(48, 740, 11, "link to page 2 (this line is the link's rectangle)"),
        pb.show_text(48, 490, 10, "highlighted line of text under a /Highlight annotation"),
        pb.show_text(48, 445, 10, "underlined line of text"),
        pb.show_text(48, 405, 10, "struck-out line of text"),
        pb.show_text(48, 300, 9, "annotations render from /AP; a page that drops /Annots looks fine"),
    ]) + b"\n"
    second_content = b"\n".join([
        _label("annots-mixed.pdf - page 2, the link destination"),
        pb.show_text(48, 700, 10, "the /Link on page 1 has a /Dest of [ this page /Fit ]"),
    ]) + b"\n"

    first = pb.content_stream(pdf, first_content)
    second = pb.content_stream(pdf, second_content)
    pdf.set(page_one, pb.dictionary([
        (b"/Type", b"/Page"), (b"/Parent", pb.ref(pages_number)),
        (b"/MediaBox", pb.rect(0, 0, pb.A4[0], pb.A4[1])),
        (b"/Resources", resources), (b"/Contents", pb.ref(first)),
        (b"/Annots", pb.array([pb.ref(number) for number in annots]))]))
    pdf.set(page_two, pb.dictionary([
        (b"/Type", b"/Page"), (b"/Parent", pb.ref(pages_number)),
        (b"/MediaBox", pb.rect(0, 0, pb.A4[0], pb.A4[1])),
        (b"/Resources", resources), (b"/Contents", pb.ref(second))]))
    pdf.set(pages_number, pb.dictionary([
        (b"/Type", b"/Pages"),
        (b"/Kids", pb.array([pb.ref(page_one), pb.ref(page_two)])), (b"/Count", b"2")]))
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number))]))
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: annotations"))


@generator("tagged.pdf")
def tagged():
    """A tagged document: /StructTreeRoot, marked-content sequences, /MarkInfo and /Lang.

    The accessibility feature set left this product on 2026-08-23 (`Features.md`), and this file is
    NOT here for it. `core/doc/TagTree` survives as Write mode's structure (decision O2), Stage 13
    needs a document whose logical order differs from its drawing order, and the corpus is where
    that document has to come from -- so the tags are content structure, not an accessibility
    claim."""
    pdf = pb.Pdf()
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    resources = _font_resource(regular, bold)

    pages_number = pdf.reserve()
    page_number = pdf.reserve()
    struct_root = pdf.reserve()

    # The drawing order below is deliberately NOT the reading order: the caption is drawn first
    # and the heading last, while the structure tree says heading, paragraphs, caption.
    content = b"\n".join([
        b"/P <</MCID 3>> BDC " + pb.show_text(48, 470, 8, "figure 1 - drawn first, read last") + b" EMC",
        b"/P <</MCID 1>> BDC " + pb.show_text(48, 700, 10, " ".join(pb.words_for(7, 14))) + b" EMC",
        b"/P <</MCID 2>> BDC " + pb.show_text(48, 680, 10, " ".join(pb.words_for(8, 14))) + b" EMC",
        b"/H1 <</MCID 0>> BDC " + _label("tagged.pdf - marked content and a structure tree") + b" EMC",
    ]) + b"\n"
    contents = pb.content_stream(pdf, content)

    kids = []
    for index, (tag, title) in enumerate(((b"/H1", "heading"), (b"/P", "first paragraph"),
                                          (b"/P", "second paragraph"), (b"/Caption", "caption"))):
        kids.append(pdf.add(pb.dictionary([
            (b"/Type", b"/StructElem"), (b"/S", tag), (b"/P", pb.ref(struct_root)),
            (b"/Pg", pb.ref(page_number)), (b"/T", pb.utf16(title)),
            (b"/K", pb.num(index))])))
    parent_tree_nums = pb.dictionary([
        (b"/Nums", pb.array([b"0", pb.array([pb.ref(kid) for kid in kids])]))])
    parent_tree = pdf.add(parent_tree_nums)
    pdf.set(struct_root, pb.dictionary([
        (b"/Type", b"/StructTreeRoot"),
        (b"/K", pb.array([pb.ref(kid) for kid in kids])),
        (b"/ParentTree", pb.ref(parent_tree)),
        (b"/ParentTreeNextKey", b"1"),
        (b"/RoleMap", pb.dictionary([(b"/Caption", b"/P")]))]))

    pdf.set(page_number, pb.dictionary([
        (b"/Type", b"/Page"), (b"/Parent", pb.ref(pages_number)),
        (b"/MediaBox", pb.rect(0, 0, pb.A4[0], pb.A4[1])),
        (b"/Resources", resources), (b"/Contents", pb.ref(contents)),
        (b"/StructParents", b"0")]))
    pdf.set(pages_number, pb.dictionary([
        (b"/Type", b"/Pages"), (b"/Kids", pb.array([pb.ref(page_number)])), (b"/Count", b"1")]))
    catalog = pdf.add(pb.dictionary([
        (b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number)),
        (b"/StructTreeRoot", pb.ref(struct_root)),
        (b"/MarkInfo", pb.dictionary([(b"/Marked", b"true")])),
        (b"/Lang", pb.literal(b"en-GB"))]))
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: tagged content"))


@generator("outline-labels.pdf")
def outline_labels():
    """A document outline (bookmarks, two levels, one open) and /PageLabels: a roman-numbered
    front matter followed by arabic body pages.

    Page labels are why the status bar cannot simply print `index + 1`: this document's fourth
    page is called "1"."""
    pdf = pb.Pdf()
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    resources = _font_resource(regular, bold)

    pages_number = pdf.reserve()
    page_numbers = [pdf.reserve() for _ in range(8)]
    labels = ["i", "ii", "iii", "1", "2", "3", "4", "5"]
    for index, number in enumerate(page_numbers):
        body = b"\n".join([
            _label("outline-labels.pdf - page %d, labelled \"%s\"" % (index + 1, labels[index])),
            pb.show_text(48, 700, 10, "index %d of 8; /PageLabels calls it %s"
                         % (index, labels[index])),
        ] + _body_block(30 + index, 20)) + b"\n"
        contents = pb.content_stream(pdf, body)
        pdf.set(number, pb.dictionary([
            (b"/Type", b"/Page"), (b"/Parent", pb.ref(pages_number)),
            (b"/MediaBox", pb.rect(0, 0, pb.A4[0], pb.A4[1])),
            (b"/Resources", resources), (b"/Contents", pb.ref(contents))]))
    pdf.set(pages_number, pb.dictionary([
        (b"/Type", b"/Pages"),
        (b"/Kids", pb.array([pb.ref(number) for number in page_numbers])),
        (b"/Count", pb.num(len(page_numbers)))]))

    outlines = pdf.reserve()
    chapter_one = pdf.reserve()
    chapter_two = pdf.reserve()
    section = pdf.reserve()
    pdf.set(section, pb.dictionary([
        (b"/Title", pb.utf16("1.1 a nested section")), (b"/Parent", pb.ref(chapter_one)),
        (b"/Dest", pb.array([pb.ref(page_numbers[4]), b"/XYZ", b"null", b"780", b"null"]))]))
    pdf.set(chapter_one, pb.dictionary([
        (b"/Title", pb.utf16("1 front matter and body")), (b"/Parent", pb.ref(outlines)),
        (b"/Next", pb.ref(chapter_two)),
        (b"/First", pb.ref(section)), (b"/Last", pb.ref(section)), (b"/Count", b"1"),
        (b"/Dest", pb.array([pb.ref(page_numbers[3]), b"/Fit"]))]))
    pdf.set(chapter_two, pb.dictionary([
        (b"/Title", pb.utf16("2 the end")), (b"/Parent", pb.ref(outlines)),
        (b"/Prev", pb.ref(chapter_one)),
        (b"/Dest", pb.array([pb.ref(page_numbers[7]), b"/Fit"]))]))
    pdf.set(outlines, pb.dictionary([
        (b"/Type", b"/Outlines"), (b"/First", pb.ref(chapter_one)),
        (b"/Last", pb.ref(chapter_two)), (b"/Count", b"3")]))

    page_labels = pb.dictionary([(b"/Nums", pb.array([
        b"0", pb.dictionary([(b"/S", b"/r")]),
        b"3", pb.dictionary([(b"/S", b"/D"), (b"/St", b"1")])]))])
    catalog = pdf.add(pb.dictionary([
        (b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number)),
        (b"/Outlines", pb.ref(outlines)), (b"/PageMode", b"/UseOutlines"),
        (b"/PageLabels", page_labels)]))
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: outline and page labels"))


# ---------------------------------------------------------------------------------------------
# Page-count and reading-direction awkwardness
# ---------------------------------------------------------------------------------------------

@generator("cover-odd-count.pdf")
def cover_odd_count():
    """Seven pages with a cover: the spread-parity case.

    In a two-page view a cover is displayed alone and the remaining six pair up 2-3, 4-5, 6-7. Get
    the parity wrong and every spread in the document is off by one -- and with an EVEN page count
    the bug is invisible, which is why the count here is odd."""
    pdf = pb.Pdf()
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    resources = _font_resource(regular, bold)

    pages = []
    for index in range(7):
        if index == 0:
            body = b"\n".join([
                pb.show_text(140, 560, 30, "COVER", b"/F2"),
                pb.show_text(120, 520, 12, "cover-odd-count.pdf - 7 pages, cover displayed alone"),
            ]) + b"\n"
        else:
            body = b"\n".join([
                _label("page %d of 7 - pairs with page %d"
                       % (index + 1, index + 2 if index % 2 else index)),
                b"0.5 w 297 60 m 297 780 l S",
            ] + _body_block(50 + index, 40)) + b"\n"
        contents = pb.content_stream(pdf, body)
        pages.append(lambda parent, c=contents: pb.page(parent, pb.A4, c, resources))

    catalog, _, _ = pb.simple_document(pdf, pages, root_extra=[
        (b"/PageLayout", b"/TwoPageRight"),
        (b"/ViewerPreferences", pb.dictionary([(b"/Direction", b"/L2R")]))])
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: cover page, odd count"))


@generator("rtl-binding.pdf")
def rtl_binding():
    """A right-to-left bound document: /ViewerPreferences /Direction /R2L with /TwoPageRight.

    RTL binding changes which side of a spread page 2 is on, and nothing else about the page. A
    layout engine that hardcodes left-to-right lays this document out backwards while every
    individual page still renders correctly -- so a per-page golden image cannot catch it and the
    fixture has to be the document."""
    pdf = pb.Pdf()
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    resources = _font_resource(regular, bold)

    pages = []
    for index in range(6):
        side = "RIGHT of the spread" if index % 2 == 0 else "LEFT of the spread"
        body = b"\n".join([
            _label("rtl-binding.pdf - page %d of 6" % (index + 1)),
            pb.show_text(48, 700, 12, "in an R2L binding this page sits on the " + side),
            pb.show_text(48, 660, 9, "the page content itself has no direction; the BINDING does"),
            b"2 w %s 60 m %s 780 l S" % (
                pb.num(pb.A4[0] - 24 if index % 2 == 0 else 24),
                pb.num(pb.A4[0] - 24 if index % 2 == 0 else 24)),
        ] + _body_block(70 + index, 34)) + b"\n"
        contents = pb.content_stream(pdf, body)
        pages.append(lambda parent, c=contents: pb.page(parent, pb.A4, c, resources))

    catalog, _, _ = pb.simple_document(pdf, pages, root_extra=[
        (b"/PageLayout", b"/TwoPageRight"),
        (b"/ViewerPreferences", pb.dictionary([(b"/Direction", b"/R2L")])),
        (b"/Lang", pb.literal(b"ar"))])
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: right-to-left binding"))


# ---------------------------------------------------------------------------------------------
# File structure
# ---------------------------------------------------------------------------------------------

@generator("objstm-xrefstream.pdf")
def objstm_xrefstream():
    """PDF 1.5 structure: every eligible object packed into an /ObjStm, and a cross-reference
    STREAM instead of a table.

    This is the modern file layout and it is a completely separate reader path from the classic
    one: type-2 xref entries, a compressed object stream, /W field widths. A corpus without it
    tests half of `XrefTable` (T7)."""
    pdf = pb.Pdf(version="1.5")
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    resources = _font_resource(regular, bold)
    pages = []
    for index in range(3):
        body = b"\n".join([
            _label("objstm-xrefstream.pdf - page %d of 3" % (index + 1)),
            pb.show_text(48, 700, 10, "objects live in an /ObjStm; the xref is a stream"),
        ] + _body_block(90 + index, 44)) + b"\n"
        contents = pb.content_stream(pdf, body)
        pages.append(lambda parent, c=contents: pb.page(parent, pb.A4, c, resources))
    catalog, _, _ = pb.simple_document(pdf, pages)
    info = pb.info_object(pdf, "RawPage corpus: object streams and xref streams")
    return pdf.serialize(catalog, info, objstm=True)


@generator("incremental-3gen.pdf")
def incremental_3gen():
    """Three generations, appended rather than rewritten -- what `tst_xreftable` (A-20) walks.

    Generation 1 is a two-page document. Generation 2 replaces page 2's content and adds a third
    page. Generation 3 replaces the /Info and page 1's content. The earlier bytes are still there,
    verbatim: a reader that rebuilds the file rather than following /Prev shows the right pages and
    is still wrong, and this is the only kind of file that can tell the difference."""
    pdf = pb.Pdf()
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    resources = _font_resource(regular, bold)

    pages_number = pdf.reserve()
    page_one = pdf.reserve()
    page_two = pdf.reserve()

    first_content = pb.content_stream(pdf, b"\n".join([
        _label("incremental-3gen.pdf - page 1, generation 1"),
        pb.show_text(48, 700, 10, "this content object is REPLACED in generation 3"),
    ]) + b"\n")
    second_content = pb.content_stream(pdf, b"\n".join([
        _label("incremental-3gen.pdf - page 2, generation 1"),
        pb.show_text(48, 700, 10, "this content object is REPLACED in generation 2"),
    ]) + b"\n")
    pdf.set(page_one, pb.page(pages_number, pb.A4, first_content, resources))
    pdf.set(page_two, pb.page(pages_number, pb.A4, second_content, resources))
    pdf.set(pages_number, pb.dictionary([
        (b"/Type", b"/Pages"),
        (b"/Kids", pb.array([pb.ref(page_one), pb.ref(page_two)])), (b"/Count", b"2")]))
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number))]))
    info = pb.info_object(pdf, "RawPage corpus: three incremental generations")
    generation_one = pdf.serialize(catalog, info)

    # Generation 2: a new page 3, page 2's content replaced, /Pages updated.
    next_number = pdf.count()
    page_three_content = next_number
    page_three = next_number + 1
    new_second = pb.Stream(
        [(b"/Filter", b"/FlateDecode")],
        pb.flate(b"\n".join([
            _label("incremental-3gen.pdf - page 2, REPLACED in generation 2"),
            pb.show_text(48, 700, 10, "generation 1's version of this object is still in the file"),
        ]) + b"\n")[0])
    third_body = pb.Stream(
        [(b"/Filter", b"/FlateDecode")],
        pb.flate(b"\n".join([
            _label("incremental-3gen.pdf - page 3, ADDED in generation 2"),
        ]) + b"\n")[0])
    generation_two = pb.incremental_update(
        generation_one,
        [(second_content, new_second),
         (page_three_content, third_body),
         (page_three, pb.page(pages_number, pb.A4, page_three_content, resources)),
         (pages_number, pb.dictionary([
             (b"/Type", b"/Pages"),
             (b"/Kids", pb.array([pb.ref(page_one), pb.ref(page_two), pb.ref(page_three)])),
             (b"/Count", b"3")]))],
        root=catalog, info=info, size=page_three + 1)

    # Generation 3: page 1's content and the document title.
    new_first = pb.Stream(
        [(b"/Filter", b"/FlateDecode")],
        pb.flate(b"\n".join([
            _label("incremental-3gen.pdf - page 1, REPLACED in generation 3"),
            pb.show_text(48, 700, 10, "three generations; the newest wins for each object"),
        ]) + b"\n")[0])
    new_info = pb.dictionary([
        (b"/Title", pb.utf16("RawPage corpus: three incremental generations (revised)")),
        (b"/Producer", pb.literal(pb.PRODUCER)),
        (b"/CreationDate", pb.literal(pb.FIXED_DATE)),
        (b"/ModDate", pb.literal(pb.FIXED_DATE))])
    return pb.incremental_update(
        generation_two, [(first_content, new_first), (info, new_info)],
        root=catalog, info=info, size=page_three + 1)


@generator("pages-900.pdf")
def pages_900():
    """900 pages. The scale case: a page tree deep enough that a flat /Kids array is a bad idea,
    and the document `M-05` scrolls through looking for a stall.

    The tree is balanced -- 30 nodes of 30 pages -- because a flat 900-entry /Kids array is legal
    and is exactly the shape that hides an O(n) page lookup behind a fast small document. Content
    streams are shared between pages: 900 distinct streams would make the file large for a reason
    that has nothing to do with what it tests."""
    pdf = pb.Pdf(version="1.5")
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    resources = _font_resource(regular, bold)

    shared = []
    for variant in range(10):
        body = b"\n".join(
            [pb.show_text(48, 700, 10, "shared body block %d of 10" % (variant + 1))]
            + _body_block(200 + variant, 46)) + b"\n"
        shared.append(pb.content_stream(pdf, body))

    root_number = pdf.reserve()
    node_numbers = [pdf.reserve() for _ in range(30)]
    page_numbers = []
    for node_index, node_number in enumerate(node_numbers):
        kids = []
        for offset in range(30):
            index = node_index * 30 + offset
            number = pdf.add(pb.dictionary([
                (b"/Type", b"/Page"), (b"/Parent", pb.ref(node_number)),
                (b"/MediaBox", pb.rect(0, 0, pb.A4[0], pb.A4[1])),
                (b"/Resources", resources),
                (b"/Contents", pb.array([pb.ref(pdf.add_stream(
                    [], pb.show_text(48, 780, 14, "page %d of 900" % (index + 1), b"/F2") + b"\n")),
                    pb.ref(shared[index % 10])]))]))
            kids.append(number)
            page_numbers.append(number)
        pdf.set(node_number, pb.dictionary([
            (b"/Type", b"/Pages"), (b"/Parent", pb.ref(root_number)),
            (b"/Kids", pb.array([pb.ref(kid) for kid in kids])), (b"/Count", b"30")]))
    pdf.set(root_number, pb.dictionary([
        (b"/Type", b"/Pages"),
        (b"/Kids", pb.array([pb.ref(number) for number in node_numbers])),
        (b"/Count", b"900")]))
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(root_number))]))
    info = pb.info_object(pdf, "RawPage corpus: 900 pages")
    return pdf.serialize(catalog, info, objstm=True)


@generator("stream-length-control.pdf")
def stream_length_control():
    """The CONTROL for `malformed/length-lies-short.pdf`, and the reason it is in `ordinary/`.

    A-17 (`Stage2-Rendering-Testing-Guide.md` section 5.3) asks for a stream whose /Length lies and
    one where it is correct, and says the second is what proves the recovery path was actually
    taken rather than the parser being length-blind. The two files are byte-for-byte identical
    except for the /Length value, which is what makes the comparison mean something -- see
    `make-malformed.py`, which builds its damaged twin from this exact content."""
    pdf = pb.Pdf()
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    resources = _font_resource(regular, bold)
    body = b"\n".join([
        _label("stream-length-control.pdf - /Length is CORRECT here"),
        pb.show_text(48, 700, 10, "malformed/length-lies-short.pdf is this file with /Length - 12"),
    ] + _body_block(300, 30)) + b"\n"
    contents = pb.content_stream(pdf, body, compressed=False)
    catalog, _, _ = pb.simple_document(
        pdf, [lambda parent: pb.page(parent, pb.A4, contents, resources)])
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: correct stream /Length"))


# ---------------------------------------------------------------------------------------------
# The writer's fixtures (Stage 3 T1)
# ---------------------------------------------------------------------------------------------

@generator("rotate-inherited.pdf")
def rotate_inherited():
    """`/Rotate 90` on the ROOT `/Pages` node, and three pages that reach it three different ways.

        root /Pages  /Rotate 90
          page 1                     no /Rotate: inherits 90 from its parent
          page 2     /Rotate 0       overrides the inherited value with an EXPLICIT zero
          /Pages                     no /Rotate of its own
            page 3                   no /Rotate: inherits 90 through TWO levels

    Decision D3-5 (`Stage3-Mutation-Plan.md` section 3) writes a rotation on the page dictionary
    and never on an ancestor, because writing it on the root would rotate the siblings -- and it
    removes a `/Rotate` it would otherwise write equal to the inherited value. Each of those is
    wrong in a way only an inherited value can show, and page 2's explicit zero is what separates
    "the key is absent" from "the key says 0", which a writer that treated both alike gets wrong.
    Page 3's grandparent is the case a one-level `/Parent` lookup misses."""
    pdf = pb.Pdf()
    _, regular, bold = _fonts(pdf)
    resources = _font_resource(regular, bold)
    root_pages = pdf.reserve()
    inner_pages = pdf.reserve()

    def contents(title, how):
        return pb.content_stream(pdf, b"\n".join([
            _label("rotate-inherited.pdf - " + title),
            pb.show_text(48, 740, 11, how),
            pb.show_text(48, 700, 11, "the arrow below points to the TOP of the unrotated page"),
            b"2 w 300 560 m 300 660 l S",
            b"280 640 m 300 665 l 320 640 l S",
            pb.show_text(48, 120, 9, "bottom-left corner marker"),
            b"1 w 40 40 m 90 40 l S  40 40 m 40 90 l S",
        ]) + b"\n")

    page_one = pdf.add(pb.page(root_pages, pb.A4, contents(
        "page 1", "no /Rotate: INHERITS 90 from the root /Pages node"), resources))
    page_two = pdf.add(pb.page(root_pages, pb.A4, contents(
        "page 2", "/Rotate 0 on the page: an EXPLICIT zero overriding the inherited 90"),
        resources, extra=[(b"/Rotate", b"0")]))
    page_three = pdf.add(pb.page(inner_pages, pb.A4, contents(
        "page 3", "no /Rotate: INHERITS 90 through an intermediate /Pages node"), resources))
    pdf.set(inner_pages, pb.dictionary([
        (b"/Type", b"/Pages"), (b"/Parent", pb.ref(root_pages)),
        (b"/Kids", pb.array([pb.ref(page_three)])), (b"/Count", b"1")]))
    pdf.set(root_pages, pb.dictionary([
        (b"/Type", b"/Pages"),
        (b"/Kids", pb.array([pb.ref(page_one), pb.ref(page_two), pb.ref(inner_pages)])),
        (b"/Count", b"3"), (b"/Rotate", b"90")]))
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(root_pages))]))
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: inherited page rotation"))


@generator("hybrid-xrefstm.pdf")
def hybrid_xrefstm():
    """A HYBRID-REFERENCE file (ISO 32000-1 section 7.5.8.4): a classic cross-reference table whose
    trailer also carries `/XRefStm`, pointing at a cross-reference stream that lists the objects
    packed in an object stream -- here the fonts, the `/Pages` node and both page dictionaries.

    Those four objects are ABSENT from the classic table, not listed as free, so they are reachable
    ONLY through `/XRefStm`. A reader that ignores the key sees a catalog whose page tree does not
    exist; nothing fails loudly, which is why `pdf/cos/XrefTable` needed a fixture rather than a
    comment saying it did (`tests/unit/tst_xreftable.cpp`). D3-5 writes a CLASSIC section after a
    file like this, and guide case A-20 requires the packed objects still to resolve afterwards.

    The other arrangement a producer may choose -- listing the packed objects in the classic table
    as FREE entries -- is not in the corpus; `corpus/PROVENANCE.md` records that limit."""
    pdf = pb.Pdf(version="1.5")
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    resources = _font_resource(regular, bold)
    pages = []
    for index in range(2):
        body = b"\n".join([
            _label("hybrid-xrefstm.pdf - page %d of 2" % (index + 1)),
            pb.show_text(48, 700, 10, "this page dictionary is in an /ObjStm that ONLY /XRefStm lists"),
        ] + _body_block(820 + index, 40, top=672)) + b"\n"
        contents = pb.content_stream(pdf, body)
        pages.append(lambda parent, c=contents: pb.page(parent, pb.A4, c, resources))
    catalog, pages_number, page_numbers = pb.simple_document(pdf, pages)
    info = pb.info_object(pdf, "RawPage corpus: hybrid-reference file")

    bodies = pdf._bodies                                              # noqa: SLF001 -- same author
    packed = sorted([regular, bold, pages_number] + page_numbers)
    objstm_number = pdf.count()
    xref_number = objstm_number + 1
    size = xref_number + 1

    header = bytearray()
    payload = bytearray()
    for number in packed:
        header += b"%d %d " % (number, len(payload))
        payload += bodies[number] + b"\n"
    objstm = pb.Stream([(b"/Type", b"/ObjStm"), (b"/N", pb.num(len(packed))),
                        (b"/First", pb.num(len(header))), (b"/Filter", b"/FlateDecode")],
                       pb.flate(bytes(header) + bytes(payload))[0])

    out = pdf._preamble(b"")                                          # noqa: SLF001
    offsets = {}
    for number in range(1, objstm_number):
        if number in packed:
            continue
        offsets[number] = len(out)
        out += pdf._render(number, bodies[number])                    # noqa: SLF001
    offsets[objstm_number] = len(out)
    out += pdf._render(objstm_number, objstm)                         # noqa: SLF001

    # The cross-reference STREAM: type-2 rows for the packed objects and nothing else, one /Index
    # subsection per contiguous run of object numbers.
    runs = []
    for number in packed:
        if runs and number == runs[-1][-1] + 1:
            runs[-1].append(number)
        else:
            runs.append([number])
    rows = bytearray()
    index_items = []
    for run in runs:
        index_items += [pb.num(run[0]), pb.num(len(run))]
        for number in run:
            rows += bytes([2]) + objstm_number.to_bytes(4, "big") \
                + packed.index(number).to_bytes(2, "big")
    encoded = pb.flate(bytes(rows))[0]
    xrefstm_offset = len(out)
    offsets[xref_number] = xrefstm_offset
    out += b"%d 0 obj\n" % xref_number
    out += pb.dictionary([(b"/Length", pb.num(len(encoded))), (b"/Type", b"/XRef"),
                          (b"/Size", pb.num(size)), (b"/W", pb.array([b"1", b"4", b"2"])),
                          (b"/Index", pb.array(index_items)), (b"/Filter", b"/FlateDecode")])
    out += b"\nstream\n" + encoded + b"\nendstream\nendobj\n"

    # The classic table: the free head, then every LOOSE object in contiguous subsections.
    classic_offset = len(out)
    out += b"xref\n0 1\n0000000000 65535 f \n"
    loose = sorted(offsets)
    subsections = []
    for number in loose:
        if subsections and number == subsections[-1][-1] + 1:
            subsections[-1].append(number)
        else:
            subsections.append([number])
    for run in subsections:
        out += b"%d %d\n" % (run[0], len(run))
        for number in run:
            out += b"%010d 00000 n \n" % offsets[number]
    trailer = [(b"/Size", pb.num(size)), (b"/Root", pb.ref(catalog)), (b"/Info", pb.ref(info)),
               (b"/XRefStm", pb.num(xrefstm_offset)), (b"/ID", pdf._file_id(bytes(out)))]  # noqa
    out += b"trailer\n" + pb.dictionary(trailer) + b"\n"
    out += b"startxref\n%d\n%%%%EOF\n" % classic_offset
    return bytes(out)


def generate():
    return {"ordinary/" + name: builder() for name, builder in FILES.items()}


if __name__ == "__main__":
    root = pathlib.Path(__file__).resolve().parent
    (root / "ordinary").mkdir(exist_ok=True)
    for relative, data in generate().items():
        target = root / relative
        target.write_bytes(data)
        print("wrote %-34s %8d bytes" % (relative, len(data)))
