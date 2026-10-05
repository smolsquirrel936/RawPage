#!/usr/bin/env python3
"""Generates Stage 6's annotation corpus -- the fixtures in `ordinary/`, their GROUND TRUTH in
`labels/annots-*.json`, the exchange files in `exchange/` and the stamp images in `stamps/`.

WHY ONE MODEL FOR FOUR OUTPUTS. `Stage6-Annotations-Plan.md` section 1 says it twice: *the oracle for
content is the generator* and *the oracle for geometry is the rendered ink*. So every annotation here
is first a Python SPEC -- subtype, rectangle, author, dates, `/NM`, thread, state, flags, quads -- and
the PDF dictionary, its `/AP`, its label entry, its FDF object and its XFDF element are all written
FROM that spec. A label cannot then describe an annotation the file does not carry, and an exchange
file cannot drift from the document it targets: the T2 reader is graded against the label, the T13
importer against the same label, and neither against PDFium's `FPDFAnnot_*` getters (rule 7).

WHAT A LABEL HOLDS (`labels/annots-<name>.json`). Per page: its media box, its `/Rotate`, and every
entry of its `/Annots` array IN ARRAY ORDER, pop-ups included, each with the object number it was
written at (`null` for a direct dictionary) and every modelled field the spec set -- the field names
are the ones `Stage6-Annotations-Testing-Guide.md` grades. A date is given as written and as UTC. A
key the model does not own is given as its RAW written value under `unknownKeys`, because D6-5's
test compares bytes. `annots-900.json` is the exception: 9,000 entries would be a 3 MB label, so it
states the formula every page follows and writes three pages out in full, as `text-900.json` does.

WHY THE EXCHANGE FILES ARE GENERATED IN ACROBAT'S SHAPES and not taken from Acrobat: D2-11 -- every
byte is ours. The shapes are the ones Acrobat 26 writes (an FDF with `/Page` in place of `/P` and no
cross-reference table, an XFDF in Adobe's namespace with `name` carrying `/NM`), and an exchange
with Acrobat itself is manual case M-04, because a file Acrobat wrote cannot enter this corpus.

DETERMINISM. No clock, no `random`, no `uuid4`, and no `math.sin`/`math.cos`: a last-bit
difference in the platform's libm would move a rounded coordinate and the file would stop
regenerating byte-identically on the Ubuntu runner (the reason `jpegenc.py` hardcodes its cosines).
Directions come from `math.sqrt`, which IEEE 754 rounds exactly; angles from fixed constants.
`/NM` values are a hash of the fixture's name and the annotation's serial.

WHAT THIS DOES NOT CLAIM. These are annotations we generated, with appearances we drew, in base-14
Helvetica. Every Stage 6 claim made over them is bounded to *our own generated corpus and the readers
named* (plan D6-26). Real annotations from real producers are met only through what Acrobat exports
in M-04.

    python corpus/make-annots.py              # write every file this generator owns
    python corpus/make-corpus.py --verify     # prove they regenerate byte-identically
"""

import datetime
import hashlib
import importlib.util
import json
import math
import pathlib
import sys
import zlib
from xml.sax.saxutils import escape, quoteattr

ROOT = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

import jpegenc                                                        # noqa: E402
import pdfbuild as pb                                                 # noqa: E402


def _load(filename):
    spec = importlib.util.spec_from_file_location(
        "_annots_" + filename.replace("-", "_").replace(".py", ""), ROOT / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# The Helvetica widths and the box convention are `make-text.py`'s, imported rather than copied a
# third time: a label's quadpoints are only as exact as the widths under them.
_TEXT = _load("make-text.py")
_COMPARE = _load("make-compare.py")
width = _TEXT.width
ASCENT = _TEXT.ASCENT
DESCENT = _TEXT.DESCENT

GENERATOR = "corpus/make-annots.py"

# ISO 32000-1 Table 165, the annotation flags.
FLAGS = {"Invisible": 1, "Hidden": 2, "Print": 4, "NoZoom": 8, "NoRotate": 16, "NoView": 32,
         "ReadOnly": 64, "Locked": 128, "ToggleNoView": 256, "LockedContents": 512}
PRINT = FLAGS["Print"]
NOTE_FLAGS = PRINT | FLAGS["NoZoom"] | FLAGS["NoRotate"]          # 28, Acrobat's note and pop-up
HIDDEN_STATE = NOTE_FLAGS | FLAGS["Hidden"]                       # 30, Acrobat's state reply

COS30 = 0.8660254037844386        # cos 30 degrees, written down rather than computed (see above)
SIN30 = 0.5
KAPPA = 0.5522847498              # the cubic Bezier quarter-circle constant

XHTML = ('<?xml version="1.0"?><body xmlns="http://www.w3.org/1999/xhtml" '
         'xmlns:xfa="http://www.xfa.org/schema/xfa-data/1.0/" xfa:APIVersion="Acrobat:26.2.0" '
         'xfa:spec="2.0.2"><p>%s</p></body>')


def r3(value):
    return round(value, 3)


# ---------------------------------------------------------------------------------------------
# Dates and identities
# ---------------------------------------------------------------------------------------------

class Date:
    """A PDF date as Acrobat writes it -- `D:YYYYMMDDHHmmSS+HH'mm'` -- and the same instant in UTC,
    which is what a sort by date must order by. Several time zones on purpose: a reader that
    compares the strings sorts these wrongly."""

    def __init__(self, day, hour, minute, zone=(8, 0), month=9, second=0):
        self.local = datetime.datetime(2026, month, day, hour, minute, second)
        self.zone = zone
        if zone is None:
            self.raw = "D:%s" % self.local.strftime("%Y%m%d%H%M%S") + "Z"
            offset = datetime.timedelta(0)
        else:
            hours, minutes = zone
            sign = "-" if hours < 0 else "+"
            self.raw = "D:%s%s%02d'%02d'" % (self.local.strftime("%Y%m%d%H%M%S"), sign,
                                             abs(hours), minutes)
            offset = datetime.timedelta(hours=hours, minutes=minutes if hours >= 0 else -minutes)
        self.utc = (self.local - offset).strftime("%Y-%m-%dT%H:%M:%SZ")


def nm_for(fixture, serial):
    """A 128-bit identifier in the 8-4-4-4-12 form Acrobat writes, from a hash -- never a clock."""
    digest = hashlib.sha256(("%s/%d" % (fixture, serial)).encode("ascii")).hexdigest()[:32]
    return "%s-%s-%s-%s-%s" % (digest[:8], digest[8:12], digest[12:16], digest[16:20], digest[20:])


def text_string(text):
    """PDFDocEncoding for ASCII, UTF-16BE with its BOM otherwise -- the two forms a reader meets."""
    try:
        text.encode("ascii")
        return pb.literal(text)
    except UnicodeEncodeError:
        return pb.utf16(text)


def numbers(values):
    return pb.array([pb.num(r3(value)) for value in values])


def box(x, baseline, size, text, bold=False):
    """The box `make-text.py` labels a run with: its exact Helvetica advance, ascent and descent."""
    return (x, baseline - DESCENT * size, x + width(text, size, bold), baseline + ASCENT * size)


def phrase_box(x, baseline, size, line, phrase):
    """The box of `phrase` inside a drawn `line`; the phrase must occur in it exactly once."""
    assert line.count(phrase) == 1, (line, phrase)
    start = x + width(line[:line.index(phrase)], size)
    return (start, baseline - DESCENT * size, start + width(phrase, size),
            baseline + ASCENT * size)


def quad(x0, y0, x1, y1):
    """QuadPoints in the order every reader actually expects (Acrobat's, not the figure in ISO
    32000-1, which shows a different one): top-left, top-right, bottom-left, bottom-right."""
    return [x0, y1, x1, y1, x0, y0, x1, y0]


def bounds(points, pad):
    xs = points[0::2]
    ys = points[1::2]
    return (min(xs) - pad, min(ys) - pad, max(xs) + pad, max(ys) + pad)


# ---------------------------------------------------------------------------------------------
# Appearance streams -- drawn in the annotation's own space, /BBox [0 0 w h]
# ---------------------------------------------------------------------------------------------

def colour_ops(colour, stroke):
    return b"%s %s %s %s" % (pb.num(r3(colour[0])), pb.num(r3(colour[1])), pb.num(r3(colour[2])),
                             b"RG" if stroke else b"rg")


def pt(x, y):
    return b"%s %s" % (pb.num(r3(x)), pb.num(r3(y)))


def ellipse(x0, y0, x1, y1):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    rx, ry = (x1 - x0) / 2, (y1 - y0) / 2
    kx, ky = rx * KAPPA, ry * KAPPA
    return b" ".join([
        pt(cx + rx, cy) + b" m",
        pt(cx + rx, cy + ky) + b" " + pt(cx + kx, cy + ry) + b" " + pt(cx, cy + ry) + b" c",
        pt(cx - kx, cy + ry) + b" " + pt(cx - rx, cy + ky) + b" " + pt(cx - rx, cy) + b" c",
        pt(cx - rx, cy - ky) + b" " + pt(cx - kx, cy - ry) + b" " + pt(cx, cy - ry) + b" c",
        pt(cx + kx, cy - ry) + b" " + pt(cx + rx, cy - ky) + b" " + pt(cx + rx, cy) + b" c"])


def unit(dx, dy):
    length = math.sqrt(dx * dx + dy * dy)
    return dx / length, dy / length


def arrow(tip, toward, size, closed):
    """An arrowhead at `tip`, its barbs swept back toward `toward` at 30 degrees."""
    ux, uy = unit(toward[0] - tip[0], toward[1] - tip[1])
    left = (tip[0] + size * (ux * COS30 - uy * SIN30), tip[1] + size * (uy * COS30 + ux * SIN30))
    right = (tip[0] + size * (ux * COS30 + uy * SIN30), tip[1] + size * (uy * COS30 - ux * SIN30))
    path = pt(*left) + b" m " + pt(*tip) + b" l " + pt(*right) + b" l"
    return path + (b" h B" if closed else b" S")


def cloud(points, bump):
    """A cloudy border (`/BE << /S /C >>`): each edge cut into near-equal spans, each span a
    semicircle bulging AWAY from the polygon's centroid, as two quarter-circle Beziers."""
    cx = sum(points[0::2]) / (len(points) / 2)
    cy = sum(points[1::2]) / (len(points) / 2)
    corners = list(zip(points[0::2], points[1::2]))
    out = [pt(*corners[0]) + b" m"]
    for index, start in enumerate(corners):
        end = corners[(index + 1) % len(corners)]
        span = math.sqrt((end[0] - start[0]) ** 2 + (end[1] - start[1]) ** 2)
        count = max(1, int(span // bump))
        ux, uy = unit(end[0] - start[0], end[1] - start[1])
        nx, ny = -uy, ux
        mid = ((start[0] + end[0]) / 2, (start[1] + end[1]) / 2)
        if (mid[0] - cx) * nx + (mid[1] - cy) * ny < 0:
            nx, ny = -nx, -ny
        step = span / count
        radius = step / 2
        for part in range(count):
            a = (start[0] + ux * step * part, start[1] + uy * step * part)
            b = (a[0] + ux * step, a[1] + uy * step)
            top = ((a[0] + b[0]) / 2 + nx * radius, (a[1] + b[1]) / 2 + ny * radius)
            k = radius * KAPPA
            out.append(pt(a[0] + nx * k, a[1] + ny * k) + b" " + pt(top[0] - ux * k, top[1] - uy * k)
                       + b" " + pt(*top) + b" c")
            out.append(pt(top[0] + ux * k, top[1] + uy * k) + b" " + pt(b[0] + nx * k, b[1] + ny * k)
                       + b" " + pt(*b) + b" c")
    return b" ".join(out) + b" h"


def note_icon(colour, w, h):
    """The Comment icon: a speech bubble with three lines, scaled to the rectangle."""
    sx, sy = w / 20.0, h / 20.0

    def p(x, y):
        return pt(x * sx, y * sy)
    return b" ".join([
        colour_ops(colour, False), b"0 G 0.6 w",
        p(1, 5) + b" m", p(1, 19) + b" l", p(19, 19) + b" l", p(19, 5) + b" l",
        p(9, 5) + b" l", p(5, 1) + b" l", p(6, 5) + b" l h B",
        b"0.8 w", p(4, 15) + b" m", p(16, 15) + b" l S", p(4, 11.5) + b" m", p(16, 11.5) + b" l S",
        p(4, 8) + b" m", p(12, 8) + b" l S"])


def text_lines(ops, words, size, x, top, limit, bold=False, font=b"/Helv"):
    """Wrapped Helvetica lines, drawn from `top` down; returns the lines drawn."""
    lines = _TEXT.wrap(words.split(), size, limit, bold)
    ops.append(b"BT %s %s Tf" % (font, pb.num(size)))
    y = top - size
    for line in lines:
        ops.append(b"1 0 0 1 %s %s Tm %s Tj" % (pb.num(r3(x)), pb.num(r3(y)), pb.literal(line)))
        y -= size * 1.2
    ops.append(b"ET")
    return lines


ROTATE_MATRIX = {90: [0, 1, -1, 0, 0, 0], 180: [-1, 0, 0, -1, 0, 0], 270: [0, -1, 1, 0, 0, 0]}


def appearance(spec):
    """(normal appearance operators, BBox width, height, /Matrix or None, needs a multiply blend).

    Drawn in the annotation's own space -- the origin at the rectangle's lower-left corner -- with a
    /BBox of the rectangle's size, which is the arrangement most producers write and the one
    Algorithm 8.1 maps onto /Rect without a matrix."""
    x0, y0, x1, y1 = spec["rect"]
    w, h = x1 - x0, y1 - y0

    def local(points):
        return [value - (x0 if index % 2 == 0 else y0) for index, value in enumerate(points)]

    kind = spec["subtype"]
    colour = spec.get("color", (0, 0, 0))
    border = spec.get("border", 1)
    ops = []
    multiply = False
    matrix = None
    bbox = (w, h)

    if kind == "Highlight":
        multiply = True
        ops.append(colour_ops(colour, False))
        q = local(spec["quads"])
        for at in range(0, len(q), 8):
            tl, tr, bl, br = q[at:at + 2], q[at + 2:at + 4], q[at + 4:at + 6], q[at + 6:at + 8]
            ops.append(pt(*tl) + b" m " + pt(*tr) + b" l " + pt(*br) + b" l " + pt(*bl) + b" l h f")
    elif kind in ("Underline", "StrikeOut", "Squiggly"):
        ops.append(colour_ops(colour, True))
        q = local(spec["quads"])
        for at in range(0, len(q), 8):
            left, right = q[at + 4], q[at + 6]
            bottom, top = q[at + 5], q[at + 1]
            thick = max(0.6, (top - bottom) / 14.0)
            ops.append(b"%s w" % pb.num(r3(thick)))
            if kind == "Underline":
                y = bottom + thick
                ops.append(pt(left, y) + b" m " + pt(right, y) + b" l S")
            elif kind == "StrikeOut":
                y = bottom + (top - bottom) * 0.42
                ops.append(pt(left, y) + b" m " + pt(right, y) + b" l S")
            else:
                path = [pt(left, bottom + 1) + b" m"]
                x, up = left, True
                while x < right:
                    x = min(right, x + 2.5)
                    path.append(pt(x, bottom + (3 if up else 1)) + b" l")
                    up = not up
                ops.append(b" ".join(path) + b" S")
    elif kind == "Square":
        inset = border / 2.0
        if "interior" in spec:
            ops.append(colour_ops(spec["interior"], False))
        ops.append(colour_ops(colour, True) + b" %s w" % pb.num(border))
        ops.append(b"%s %s %s %s re %s" % (pb.num(inset), pb.num(inset), pb.num(r3(w - border)),
                                           pb.num(r3(h - border)),
                                           b"B" if "interior" in spec else b"S"))
    elif kind == "Circle":
        inset = border / 2.0
        if "interior" in spec:
            ops.append(colour_ops(spec["interior"], False))
        ops.append(colour_ops(colour, True) + b" %s w" % pb.num(border))
        ops.append(ellipse(inset, inset, w - inset, h - inset)
                   + (b" B" if "interior" in spec else b" S"))
    elif kind == "Line":
        a = local(spec["line"])
        start, end = (a[0], a[1]), (a[2], a[3])
        ops.append(colour_ops(colour, True) + b" %s w" % pb.num(border))
        if "interior" in spec:
            ops.append(colour_ops(spec["interior"], False))
        ops.append(pt(*start) + b" m " + pt(*end) + b" l S")
        heads = spec.get("endings", ("None", "None"))
        for tip, other, style in ((start, end, heads[0]), (end, start, heads[1])):
            if style in ("OpenArrow", "ClosedArrow"):
                ops.append(arrow(tip, other, 9, style == "ClosedArrow"))
        if spec.get("intent") == "LineDimension":
            mid = ((start[0] + end[0]) / 2, (start[1] + end[1]) / 2)
            label = spec["contents"]
            ops.append(b"0 g BT /Helv 8 Tf 1 0 0 1 %s %s Tm %s Tj ET" % (
                pb.num(r3(mid[0] - width(label, 8) / 2)), pb.num(r3(mid[1] + 3)),
                pb.literal(label)))
    elif kind in ("Polygon", "PolyLine"):
        v = local(spec["vertices"])
        ops.append(colour_ops(colour, True) + b" %s w 1 j" % pb.num(border))
        if "interior" in spec:
            ops.append(colour_ops(spec["interior"], False))
        if spec.get("cloud"):
            ops.append(cloud(v, 10.0) + (b" B" if "interior" in spec else b" S"))
        else:
            path = [pt(v[0], v[1]) + b" m"] + [pt(v[i], v[i + 1]) + b" l"
                                               for i in range(2, len(v), 2)]
            if kind == "Polygon":
                ops.append(b" ".join(path) + (b" h B" if "interior" in spec else b" h S"))
            else:
                ops.append(b" ".join(path) + b" S")
        if spec.get("intent") in ("PolyLineDimension", "PolygonDimension"):
            label = spec["contents"]
            y = h - 14 if spec["intent"] == "PolyLineDimension" else h * 0.25
            ops.append(b"0 g BT /Helv 8 Tf 1 0 0 1 %s %s Tm %s Tj ET" % (
                pb.num(r3(w / 2 - width(label, 8) / 2)), pb.num(r3(y)), pb.literal(label)))
    elif kind == "Ink":
        ops.append(colour_ops(colour, True) + b" %s w 1 J 1 j" % pb.num(border))
        for stroke in spec["ink"]:
            s = local(stroke)
            ops.append(b" ".join([pt(s[0], s[1]) + b" m"]
                                 + [pt(s[i], s[i + 1]) + b" l" for i in range(2, len(s), 2)])
                       + b" S")
    elif kind == "Caret":
        ops.append(colour_ops(colour, False))
        ops.append(pt(0, 0) + b" m " + pt(w / 2, h) + b" l " + pt(w, 0) + b" l "
                   + pt(w / 2, h * 0.3) + b" l h f")
    elif kind == "Text":
        ops.append(note_icon(colour, w, h))
    elif kind == "FreeText":
        rotate = spec.get("rotate", 0)
        bw, bh = (w, h) if rotate in (0, 180) else (h, w)
        bbox = (bw, bh)
        if rotate:
            matrix = ROTATE_MATRIX[rotate]
        inner = (0, 0, bw, bh)
        if spec.get("intent") == "FreeTextCallout":
            rd = spec["rd"]
            inner = (rd[0], rd[1], bw - rd[2], bh - rd[3])
            c = local(spec["callout"])
            ops.append(colour_ops(colour, True) + b" 1 w")
            ops.append(pt(c[0], c[1]) + b" m " + pt(c[2], c[3]) + b" l " + pt(c[4], c[5]) + b" l S")
            ops.append(arrow((c[0], c[1]), (c[2], c[3]), 8, False))
        size = spec.get("font_size", 10)
        if spec.get("intent") != "FreeTextTypeWriter":
            ops.append(b"1 1 0.92 rg %s %s %s %s re f" % (
                pb.num(r3(inner[0])), pb.num(r3(inner[1])), pb.num(r3(inner[2] - inner[0])),
                pb.num(r3(inner[3] - inner[1]))))
            ops.append(colour_ops(colour, True) + b" %s w %s %s %s %s re S" % (
                pb.num(border), pb.num(r3(inner[0] + border / 2)), pb.num(r3(inner[1] + border / 2)),
                pb.num(r3(inner[2] - inner[0] - border)), pb.num(r3(inner[3] - inner[1] - border))))
        ops.append(b"0 g")
        text_lines(ops, spec.get("ap_text", spec["contents"]), size, inner[0] + 4, inner[3] - 3,
                   inner[2] - inner[0] - 8)
    elif kind == "Stamp":
        green = (0.11, 0.45, 0.15)
        label = spec.get("stamp_text", "APPROVED")
        size = 20
        ops.append(colour_ops(green, True) + b" 0.9 0.97 0.9 rg 2.5 w")
        ops.append(b"2 2 %s %s re B" % (pb.num(r3(w - 4)), pb.num(r3(h - 4))))
        ops.append(colour_ops(green, False) + b" BT /HeBo %d Tf 1 0 0 1 %s %s Tm %s Tj ET" % (
            size, pb.num(r3((w - width(label, size, True)) / 2)), pb.num(r3(h / 2 - 7)),
            pb.literal(label)))
    elif kind == "FileAttachment":
        ops.append(colour_ops(colour, True) + b" 1.2 w 1 J")
        ops.append(colour_ops(colour, False))
        ops.append(ellipse(w / 2 - 5, h - 11, w / 2 + 5, h - 1) + b" B")
        ops.append(pt(w / 2, h - 11) + b" m " + pt(w / 2, 1) + b" l S")
    elif kind == "Sound":
        ops.append(colour_ops(colour, False) + b" 0 G 0.6 w")
        ops.append(b" ".join([pt(2, h * 0.35) + b" m", pt(7, h * 0.35) + b" l",
                              pt(12, h * 0.1) + b" l", pt(12, h * 0.9) + b" l",
                              pt(7, h * 0.65) + b" l", pt(2, h * 0.65) + b" l h B"]))
        ops.append(pt(15, h * 0.3) + b" m " + pt(18, h * 0.5) + b" l " + pt(15, h * 0.7) + b" l S")
    elif kind == "Redact":
        ops.append(b"1 0 0 RG 1 w 0.5 0.5 %s %s re S" % (pb.num(r3(w - 1)), pb.num(r3(h - 1))))
    else:
        raise ValueError("no appearance for %s" % kind)
    return b"\n".join(ops) + b"\n", r3(bbox[0]), r3(bbox[1]), matrix, multiply


def redact_overlay(spec):
    """/RO: what the mark will look like once applied -- the fill and the overlay text."""
    x0, y0, x1, y1 = spec["rect"]
    w, h = x1 - x0, y1 - y0
    ops = [colour_ops(spec["interior"], False), b"0 0 %s %s re f" % (pb.num(r3(w)), pb.num(r3(h)))]
    if spec.get("overlay"):
        ops.append(b"1 g BT /Helv 9 Tf 1 0 0 1 3 %s Tm %s Tj ET" % (
            pb.num(r3(h / 2 - 3)), pb.literal(spec["overlay"])))
    return b"\n".join(ops) + b"\n"


# ---------------------------------------------------------------------------------------------
# Embedded payloads
# ---------------------------------------------------------------------------------------------

def pcm_triangle(samples=2000, period=40, amplitude=8000):
    """16-bit signed big-endian PCM (ISO 32000-1 section 13.3: most significant bits first), a
    triangle wave in integer arithmetic -- a sine would need libm."""
    out = bytearray()
    for index in range(samples):
        phase = index % period
        quarter = period // 4
        if phase < quarter:
            value = amplitude * phase // quarter
        elif phase < 3 * quarter:
            value = amplitude - amplitude * (phase - quarter) // quarter
        else:
            value = -amplitude + amplitude * (phase - 3 * quarter) // quarter
        out += value.to_bytes(2, "big", signed=True)
    return bytes(out)


def png_rgba(width_px, height_px, pixel):
    """A PNG, colour type 6 (RGBA), filter 0 on every row, compressed by `deflate.py` -- the zlib
    stream PNG's IDAT holds is the same RFC 1950 stream `/FlateDecode` wants. CRCs by
    `zlib.crc32`, which is a fixed function (unlike `zlib.compress`, whose output is the build's)."""
    import deflate
    raw = bytearray()
    for y in range(height_px):
        raw.append(0)
        for x in range(width_px):
            raw += bytes(pixel(x, y))

    def chunk(kind, payload):
        return (len(payload).to_bytes(4, "big") + kind + payload
                + (zlib.crc32(kind + payload) & 0xFFFFFFFF).to_bytes(4, "big"))
    header = (width_px.to_bytes(4, "big") + height_px.to_bytes(4, "big")
              + bytes([8, 6, 0, 0, 0]))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", deflate.compress(bytes(raw))) + chunk(b"IEND", b""))


def stamp_seal_png():
    """96 x 96 RGBA: a red ring with a solid centre, transparent outside it and translucent in a
    band between -- so alpha takes 0, 255 and values between, which is what an `/SMask` must carry."""
    size = 96
    centre = 47.5

    def pixel(x, y):
        d2 = (x - centre) ** 2 + (y - centre) ** 2
        if d2 > 46 * 46:
            return (255, 255, 255, 0)
        if d2 > 40 * 40:
            return (190, 20, 30, 255)
        if d2 > 34 * 34:
            return (190, 20, 30, 96)
        if d2 > 18 * 18:
            return (255, 255, 255, 0)
        return (190, 20, 30, 255)
    return png_rgba(size, size, pixel)


def stamp_signature_jpeg():
    """160 x 60 baseline RGB JPEG: dark ink strokes on off-white paper, as a scanned signature."""
    w, h = 160, 60

    def ink(x, y):
        # Three strokes drawn as integer bands: a rising slope, a hump, an underline.
        return (abs((y - 45) * 4 - (x - 10) * 3) < 12 and 10 <= x <= 60) \
            or (abs((y - 30) - abs(x - 95) // 2) < 3 and 70 <= x <= 120) \
            or (49 <= y <= 52 and 20 <= x <= 150)
    # A 5 x 5 box blur of the ink, in integers: a scan's edges are soft, and a hard one-pixel edge
    # rings past jpegenc's self-check tolerance (measured: 70 levels against 60).
    samples = []
    for y in range(h):
        for x in range(w):
            cover = sum(1 for dy in range(-2, 3) for dx in range(-2, 3) if ink(x + dx, y + dy))
            samples += [248 - (248 - 28) * cover // 25, 246 - (246 - 32) * cover // 25,
                        238 - (238 - 70) * cover // 25]
    blob = jpegenc.encode(w, h, samples, 3)
    jpegenc.selfcheck(w, h, samples, 3, blob, tolerance=60)
    return blob


# ---------------------------------------------------------------------------------------------
# The fixture builder
# ---------------------------------------------------------------------------------------------

class FixturePage:
    def __init__(self, media, rotate):
        self.media = media
        self.rotate = rotate
        self.ops = []
        self.listing = []           # the /Annots array, in order: specs (an entry may be shared)
        self.number = None

    def text(self, x, y, size, text, bold=False):
        self.ops.append(pb.show_text(x, y, size, text, b"/F2" if bold else b"/F1"))
        return box(x, y, size, text, bold)


class Fixture:
    """One annotated PDF: pages, their content, and annotation specs written in two passes -- object
    numbers first, so a reply can name a parent written later, then bodies."""

    def __init__(self, name, title, author, subject, date):
        self.name = name
        self.title = title
        self.author = author
        self.subject = subject
        self.date = date
        self.pages = []
        self.specs = []
        self.serial = 0
        self.claims = []

    def page(self, media=pb.A4, rotate=0):
        page = FixturePage(media, rotate)
        self.pages.append(page)
        return page

    def annot(self, page, subtype, rect, **fields):
        """Adds an annotation to `page`'s /Annots; returns its spec. Every annotation gets an /NM
        unless `nm=None` is passed -- real files without one are what `annots-direct.pdf` holds."""
        self.serial += 1
        spec = {"subtype": subtype, "rect": tuple(r3(v) for v in rect), "page": page,
                "serial": self.serial}
        if "nm" not in fields and subtype != "Popup":
            fields["nm"] = nm_for(self.name, self.serial)
        if fields.get("nm") is None:
            fields.pop("nm", None)
        fields.setdefault("flags", NOTE_FLAGS if subtype in ("Text", "Popup") else PRINT)
        unlisted = fields.pop("unlisted", False)
        spec.update(fields)
        if not unlisted:          # an unlisted annotation is written, and no page's /Annots names it
            page.listing.append(spec)
        self.specs.append(spec)
        return spec

    def popup(self, parent, rect, open_=False):
        popup = self.annot(parent["page"], "Popup", rect, parent=parent, open=open_)
        parent["popup"] = popup
        return popup

    # -- serialization ----------------------------------------------------------------------

    def build(self, objstm=False, root_extra=None, resources_extra=None):
        """Serializes the fixture. `resources_extra(pdf)` returns extra page-resource pairs (the
        migration fixture's image); `root_extra(pdf)` extra catalog pairs (an /EmbeddedFiles tree)
        -- callables, because what they name must be allocated in this Pdf."""
        pdf = pb.Pdf()
        regular = pb.base14(pdf, "Helvetica")
        bold = pb.base14(pdf, "Helvetica-Bold")
        fonts = pb.dictionary([(b"/F1", pb.ref(regular)), (b"/F2", pb.ref(bold))])
        page_resources = pb.dictionary([(b"/Font", fonts)]
                                       + (resources_extra(pdf) if resources_extra else []))
        ap_fonts = pb.dictionary([(b"/Helv", pb.ref(regular)), (b"/HeBo", pb.ref(bold))])
        self.ap_fonts = ap_fonts
        self.pdf = pdf

        pages_number = pdf.reserve()
        for page in self.pages:
            page.number = pdf.reserve()
        for spec in self.specs:
            spec["obj"] = None if spec.get("direct") else pdf.reserve()

        shared_aps = {}
        bodies = {}
        for spec in self.specs:
            bodies[spec["serial"]] = self.annot_dict(spec, "pdf", shared_aps)
            if spec["obj"] is not None:
                pdf.set(spec["obj"], bodies[spec["serial"]])

        for index, page in enumerate(self.pages):
            contents = pb.content_stream(pdf, b"\n".join(page.ops) + b"\n")
            extra = []
            if page.rotate:
                extra.append((b"/Rotate", pb.num(page.rotate)))
            if page.listing:
                extra.append((b"/Annots", pb.array([
                    bodies[spec["serial"]] if spec["obj"] is None else pb.ref(spec["obj"])
                    for spec in page.listing])))
            pdf.set(page.number, pb.page(pages_number, page.media, contents, page_resources,
                                         extra))
        pdf.set(pages_number, pb.dictionary([
            (b"/Type", b"/Pages"), (b"/Kids", pb.array([pb.ref(p.number) for p in self.pages])),
            (b"/Count", pb.num(len(self.pages)))]))
        catalog = pdf.add(pb.dictionary(
            [(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number))]
            + (root_extra(pdf) if root_extra else [])))
        info = pdf.add(pb.dictionary([
            (b"/Title", pb.utf16(self.title)), (b"/Author", pb.utf16(self.author)),
            (b"/Subject", pb.utf16(self.subject)), (b"/Producer", pb.literal(pb.PRODUCER)),
            (b"/CreationDate", pb.literal(self.date)), (b"/ModDate", pb.literal(self.date))]))
        data = pdf.serialize(catalog, info, objstm=objstm)
        self.file_id = data.rsplit(b"/ID [ <", 1)[1].split(b">", 1)[0].decode("ascii")
        return data

    def appearance_stream(self, pdf, spec, shared):
        ops, bw, bh, matrix, multiply = appearance(spec)
        key = (ops, bw, bh, tuple(matrix) if matrix else None, multiply, spec.get("opacity"))
        if spec.get("share_ap") and key in shared:
            return shared[key]
        gs = []
        if multiply:
            gs.append((b"/BM", b"/Multiply"))
        if spec.get("opacity") is not None:
            gs += [(b"/CA", pb.num(spec["opacity"])), (b"/ca", pb.num(spec["opacity"]))]
        resources = [(b"/Font", self.ap_fonts)]
        if gs:
            resources.append((b"/ExtGState", pb.dictionary([(b"/GS0", pb.dictionary(gs))])))
            ops = b"/GS0 gs\n" + ops
        pairs = [(b"/Type", b"/XObject"), (b"/Subtype", b"/Form"), (b"/BBox", pb.rect(0, 0, bw, bh))]
        if matrix:
            pairs.append((b"/Matrix", numbers(matrix)))
        data, filter_name = pb.flate(ops)
        pairs += [(b"/Resources", pb.dictionary(resources)), (b"/Filter", filter_name)]
        number = pdf.add_stream(pairs, data)
        if spec.get("share_ap"):
            shared[key] = number
        return number

    def annot_dict(self, spec, mode, shared, ref_of=None, fdf_pdf=None):
        """The annotation's dictionary. `mode` is "pdf" or "fdf": an FDF annotation names its page
        by index (`/Page`) rather than by reference, carries no appearance (an importer generates
        one), and refers to its parent and pop-up by FDF object number."""
        pdf = fdf_pdf if mode == "fdf" else self.pdf
        ref_of = ref_of or (lambda other: other["obj"])
        pairs = [(b"/Type", b"/Annot"), (b"/Subtype", pb.name(spec["subtype"])),
                 (b"/Rect", numbers(spec["rect"]))]
        page_index = self.pages.index(spec["page"])
        if mode == "fdf":
            pairs.append((b"/Page", pb.num(page_index)))
        elif not spec.get("no_p"):
            pairs.append((b"/P", pb.ref(spec["page"].number)))
        pairs.append((b"/F", pb.num(spec["flags"])))
        if "nm" in spec:
            pairs.append((b"/NM", pb.literal(spec["nm"])))
        if "author" in spec:
            pairs.append((b"/T", text_string(spec["author"])))
        if "subject" in spec:
            pairs.append((b"/Subj", text_string(spec["subject"])))
        if "contents" in spec:
            pairs.append((b"/Contents", text_string(spec["contents"])))
        if "rich" in spec:
            pairs.append((b"/RC", pb.literal((XHTML % escape(spec["rich"])).encode("ascii"))))
        if "created" in spec:
            pairs.append((b"/CreationDate", pb.literal(spec["created"].raw)))
        if "modified" in spec:
            pairs.append((b"/M", pb.literal(spec["modified"].raw)))
        if "color" in spec:
            pairs.append((b"/C", numbers(spec["color"])))
        if "interior" in spec:
            pairs.append((b"/IC", numbers(spec["interior"])))
        if spec.get("opacity") is not None:
            pairs.append((b"/CA", pb.num(spec["opacity"])))
        if "border" in spec:
            pairs.append((b"/BS", pb.dictionary([(b"/W", pb.num(spec["border"])),
                                                 (b"/S", b"/S")])))
        if spec.get("cloud"):
            pairs.append((b"/BE", pb.dictionary([(b"/S", b"/C"), (b"/I", pb.num(spec["cloud"]))])))
        if "quads" in spec:
            pairs.append((b"/QuadPoints", numbers(spec["quads"])))
        if "ink" in spec:
            pairs.append((b"/InkList", pb.array([numbers(s) for s in spec["ink"]])))
        if "vertices" in spec:
            pairs.append((b"/Vertices", numbers(spec["vertices"])))
        if "line" in spec:
            pairs.append((b"/L", numbers(spec["line"])))
        if "endings" in spec:
            pairs.append((b"/LE", pb.array([pb.name(e) for e in spec["endings"]])))
        if "leader" in spec:
            pairs += [(b"/LL", pb.num(spec["leader"])), (b"/Cap", b"true")]
        if "intent" in spec:
            pairs.append((b"/IT", pb.name(spec["intent"])))
        if "icon" in spec:
            pairs.append((b"/Name", pb.name(spec["icon"])))
        if "da" in spec:
            pairs.append((b"/DA", pb.literal(spec["da"])))
        if "ds" in spec:
            pairs.append((b"/DS", pb.literal(spec["ds"])))
        if "justify" in spec:
            pairs.append((b"/Q", pb.num(spec["justify"])))
        if "callout" in spec:
            pairs.append((b"/CL", numbers(spec["callout"])))
        if "rd" in spec:
            pairs.append((b"/RD", numbers(spec["rd"])))
        if spec.get("rotate"):
            pairs.append((b"/Rotate", pb.num(spec["rotate"])))
        if "symbol" in spec:
            pairs.append((b"/Sy", pb.name(spec["symbol"])))
        if "irt" in spec:
            pairs.append((b"/IRT", pb.ref(ref_of(spec["irt"]))))
        if "irt_missing" in spec:
            pairs.append((b"/IRT", pb.ref(spec["irt_missing"])))
        if "rt" in spec:
            pairs.append((b"/RT", pb.name(spec["rt"])))
        if "state_model" in spec:
            # TEXT STRINGS, as ISO 32000-1 Table 172 says and Acrobat writes -- not names. Written as
            # names until Stage 6 T2, whose reader is lenient about it and found it.
            pairs += [(b"/StateModel", text_string(spec["state_model"])),
                      (b"/State", text_string(spec["state"]))]
        if "popup" in spec:
            pairs.append((b"/Popup", pb.ref(ref_of(spec["popup"]))))
        if "parent" in spec:
            pairs.append((b"/Parent", pb.ref(ref_of(spec["parent"]))))
        if "open" in spec:
            pairs.append((b"/Open", b"true" if spec["open"] else b"false"))
        if "measure" in spec:
            pairs.append((b"/Measure", measure_dict(spec["measure"])))
        if "overlay" in spec:
            pairs += [(b"/OverlayText", pb.literal(spec["overlay"])),
                      (b"/Repeat", b"true" if spec.get("repeat") else b"false")]
        if "attachment" in spec:
            pairs.append((b"/FS", pb.ref(file_spec(pdf, spec["attachment"]))))
        if "sound" in spec:
            sound = spec["sound"]
            pairs.append((b"/Sound", pb.ref(pdf.add_stream([
                (b"/Type", b"/Sound"), (b"/R", pb.num(sound["rate"])),
                (b"/C", pb.num(sound["channels"])), (b"/B", pb.num(sound["bits"])),
                (b"/E", b"/Signed")], sound["data"]))))
        for key, value in spec.get("extra", ()):
            pairs.append((key, value))
        if mode == "pdf" and spec["subtype"] != "Popup" and spec.get("ap", True):
            ap = [(b"/N", pb.ref(self.appearance_stream(pdf, spec, shared)))]
            pairs.append((b"/AP", pb.dictionary(ap)))
            if spec["subtype"] == "Redact":
                data, filter_name = pb.flate(redact_overlay(spec))
                x0, y0, x1, y1 = spec["rect"]
                overlay = pdf.add_stream([
                    (b"/Type", b"/XObject"), (b"/Subtype", b"/Form"),
                    (b"/BBox", pb.rect(0, 0, r3(x1 - x0), r3(y1 - y0))),
                    (b"/Resources", pb.dictionary([(b"/Font", self.ap_fonts)])),
                    (b"/Filter", filter_name)], data)
                pairs.append((b"/RO", pb.ref(overlay)))
        return pb.dictionary(pairs)

    # -- the label ------------------------------------------------------------------------------

    def label_entry(self, spec, index):
        out = {"index": index, "object": spec["obj"], "subtype": spec["subtype"],
               "rect": list(spec["rect"]), "flags": spec["flags"],
               "flagNames": [n for n, bit in FLAGS.items() if spec["flags"] & bit]}
        simple = [("nm", "nm"), ("author", "author"), ("subject", "subject"),
                  ("contents", "contents"), ("rich", "richText"), ("color", "color"),
                  ("interior", "interiorColor"), ("opacity", "opacity"), ("border", "borderWidth"),
                  ("cloud", "cloudIntensity"), ("intent", "intent"), ("icon", "icon"),
                  ("da", "defaultAppearance"), ("ds", "defaultStyle"), ("justify", "justification"),
                  ("rt", "replyType"), ("state_model", "stateModel"), ("state", "state"),
                  ("open", "open"), ("rotate", "rotate"), ("symbol", "caretSymbol"),
                  ("overlay", "overlayText"), ("repeat", "repeat"), ("leader", "leaderLength"),
                  ("endings", "lineEndings")]
        for key, name in simple:
            if key in spec:
                value = spec[key]
                out[name] = list(value) if isinstance(value, tuple) else value
        for key, name in (("quads", "quadPoints"), ("vertices", "vertices"), ("line", "line"),
                          ("callout", "callout"), ("rd", "rectDifferences")):
            if key in spec:
                out[name] = [r3(v) for v in spec[key]]
        if "ink" in spec:
            out["inkList"] = [[r3(v) for v in stroke] for stroke in spec["ink"]]
        if "created" in spec:
            out["created"] = spec["created"].raw
            out["createdUtc"] = spec["created"].utc
        if "modified" in spec:
            out["modified"] = spec["modified"].raw
            out["modifiedUtc"] = spec["modified"].utc
        if "irt" in spec:
            parent = spec["irt"]
            out["inReplyTo"] = {"object": parent["obj"], "nm": parent.get("nm"),
                                "page": self.pages.index(parent["page"])}
        if "irt_missing" in spec:
            out["inReplyTo"] = {"object": spec["irt_missing"], "missing": True}
        if "popup" in spec:
            out["popup"] = spec["popup"]["obj"]
        if "parent" in spec:
            out["parent"] = spec["parent"]["obj"]
        if "measure" in spec:
            out["measure"] = spec["measure"]
        if "attachment" in spec:
            a = spec["attachment"]
            out["attachment"] = {"name": a["name"], "mime": a["mime"], "size": len(a["data"]),
                                 "sha256": hashlib.sha256(a["data"]).hexdigest()}
            if a.get("desc"):
                out["attachment"]["description"] = a["desc"]
        if "sound" in spec:
            s = spec["sound"]
            out["sound"] = {"rate": s["rate"], "channels": s["channels"], "bits": s["bits"],
                            "encoding": "Signed", "byteOrder": "big-endian",
                            "samples": len(s["data"]) * 8 // (s["bits"] * s["channels"]),
                            "sha256": hashlib.sha256(s["data"]).hexdigest()}
        if spec.get("extra"):
            out["unknownKeys"] = {key.decode("ascii"): value.decode("latin-1")
                                  for key, value in spec["extra"]}
        if spec.get("direct"):
            out["direct"] = True
        if spec.get("no_p"):
            out["noP"] = True
        listed = [i for i, p in enumerate(self.pages) if spec in p.listing]
        if len(listed) > 1:
            out["listedBy"] = listed
        out["hasAppearance"] = spec["subtype"] != "Popup" and spec.get("ap", True)
        return out

    def label(self, **top):
        pages = []
        for index, page in enumerate(self.pages):
            pages.append({"index": index, "mediaBox": [0, 0, r3(page.media[0]), r3(page.media[1])],
                          "rotate": page.rotate,
                          "annots": [self.label_entry(spec, position)
                                     for position, spec in enumerate(page.listing)]})
        unlisted = [self.label_entry(spec, -1) for spec in self.specs
                    if not any(spec in page.listing for page in self.pages)]
        if unlisted:
            top = dict(top, unlisted=unlisted)
        body = {"file": "ordinary/" + self.name, "generator": GENERATOR,
                "units": "PDF points, page space (before /Rotate)",
                "fileId": self.file_id, "claims": self.claims, **top, "pages": pages}
        return (json.dumps(body, ensure_ascii=False, indent=1) + "\n").encode("utf-8")


def measure_dict(measure):
    """A rectilinear measure dictionary, ISO 32000-1 section 12.9: the scale string and a number
    format per quantity, each converting from default user space units (points)."""
    def fmt(entry):
        # Seven decimals, not pb.num's four: 1/18 written as 0.0556 would make 72 pt measure
        # 4.0032 ft, and the reading would be wrong in its third digit.
        factor = entry["factor"]
        written = pb.num(factor) if isinstance(factor, int) else \
            ("%.7f" % factor).rstrip("0").encode("ascii")
        return pb.dictionary([(b"/Type", b"/NumberFormat"), (b"/U", pb.literal(entry["unit"])),
                              (b"/C", written), (b"/D", pb.num(entry["precision"]))])
    return pb.dictionary([
        (b"/Type", b"/Measure"), (b"/Subtype", b"/RL"), (b"/R", pb.literal(measure["ratio"])),
        (b"/X", pb.array([fmt(measure["x"])])), (b"/D", pb.array([fmt(measure["distance"])])),
        (b"/A", pb.array([fmt(measure["area"])]))])


def file_spec(pdf, attachment):
    payload = attachment["data"]
    data, filter_name = pb.flate(payload)
    params = [(b"/Size", pb.num(len(payload))),
              (b"/CheckSum", pb.hexstring(hashlib.md5(payload).digest()))]
    if attachment.get("modified"):
        params.append((b"/ModDate", pb.literal(attachment["modified"].raw)))
    stream = pdf.add_stream([(b"/Type", b"/EmbeddedFile"), (b"/Subtype", pb.name(attachment["mime"])),
                             (b"/Params", pb.dictionary(params)), (b"/Filter", filter_name)], data)
    pairs = [(b"/Type", b"/Filespec"), (b"/F", pb.literal(attachment["name"])),
             (b"/UF", pb.utf16(attachment["name"])),
             (b"/EF", pb.dictionary([(b"/F", pb.ref(stream))]))]
    if attachment.get("desc"):
        pairs.append((b"/Desc", text_string(attachment["desc"])))
    return pdf.add(pb.dictionary(pairs))


# ---------------------------------------------------------------------------------------------
# Exchange files -- FDF and XFDF in the shapes Acrobat writes
# ---------------------------------------------------------------------------------------------

def fdf_bytes(fixture):
    """An FDF over every annotation of `fixture`: one catalog naming the target file and its /ID,
    one indirect object per annotation (pop-ups too), no cross-reference table -- Acrobat writes
    none, and a reader that insists on one is wrong about FDF (ISO 32000-1 section 12.7.8)."""
    fdf = pb.Pdf()
    catalog = fdf.reserve()
    numbers_by_serial = {spec["serial"]: fdf.reserve() for spec in fixture.specs}

    def ref_of(other):
        return numbers_by_serial[other["serial"]]
    for spec in fixture.specs:
        fdf.set(numbers_by_serial[spec["serial"]],
                fixture.annot_dict(spec, "fdf", {}, ref_of=ref_of, fdf_pdf=fdf))
    target = fixture.name
    fdf.set(catalog, pb.dictionary([
        (b"/FDF", pb.dictionary([
            (b"/Annots", pb.array([pb.ref(numbers_by_serial[s["serial"]]) for s in fixture.specs])),
            (b"/F", pb.literal(target)), (b"/UF", pb.utf16(target)),
            (b"/ID", pb.array([b"<" + fixture.file_id.encode() + b">"] * 2))])),
        (b"/Type", b"/Catalog")]))
    out = bytearray(b"%FDF-1.2\n%\xe2\xe3\xcf\xd3\n")
    for number in range(1, fdf.count()):
        out += fdf._render(number, fdf._bodies[number])         # noqa: SLF001 -- same author
    out += b"trailer\n" + pb.dictionary([(b"/Root", pb.ref(catalog))]) + b"\n%%EOF\n"
    return bytes(out)


def _hex_colour(colour):
    return "#%02X%02X%02X" % tuple(int(round(c * 255)) for c in colour)


def _xnum(value):
    return pb.num(r3(value)).decode("ascii")


def _xlist(values):
    return ",".join(_xnum(v) for v in values)


XFDF_ELEMENT = {"Text": "text", "FreeText": "freetext", "Line": "line", "Square": "square",
                "Circle": "circle", "Polygon": "polygon", "PolyLine": "polyline", "Ink": "ink",
                "Highlight": "highlight", "Underline": "underline", "StrikeOut": "strikeout",
                "Squiggly": "squiggly", "Caret": "caret", "Stamp": "stamp",
                "FileAttachment": "fileattachment", "Sound": "sound", "Redact": "redact"}


def _xfdf_flags(flags):
    return ",".join(n.lower() for n, bit in FLAGS.items() if flags & bit)


def xfdf_bytes(fixture, missing_parent_nm="missing-parent"):
    """An XFDF over every annotation of `fixture`, in Adobe's namespace: `name` carries /NM,
    `inreplyto` a parent's /NM, `replyType` "group" for /RT /Group, a pop-up as a child element."""
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<xfdf xmlns="http://ns.adobe.com/xfdf/" xml:space="preserve">', "<annots>"]
    for spec in fixture.specs:
        if spec["subtype"] == "Popup":
            continue
        attrs = [("color", _hex_colour(spec["color"])) if "color" in spec else None,
                 ("creationdate", spec["created"].raw) if "created" in spec else None,
                 ("flags", _xfdf_flags(spec["flags"])),
                 ("date", spec["modified"].raw) if "modified" in spec else None,
                 ("name", spec["nm"]) if "nm" in spec else None,
                 ("page", str(fixture.pages.index(spec["page"]))),
                 ("rect", _xlist(spec["rect"])),
                 ("subject", spec["subject"]) if "subject" in spec else None,
                 ("title", spec["author"]) if "author" in spec else None,
                 ("opacity", _xnum(spec["opacity"])) if spec.get("opacity") is not None else None,
                 ("width", _xnum(spec["border"])) if "border" in spec else None,
                 ("interior-color", _hex_colour(spec["interior"])) if "interior" in spec else None,
                 ("coords", _xlist(spec["quads"])) if "quads" in spec else None,
                 ("icon", spec["icon"]) if "icon" in spec else None,
                 ("intent", spec["intent"]) if "intent" in spec else None,
                 ("rotation", str(spec["rotate"])) if spec.get("rotate") else None,
                 ("callout", _xlist(spec["callout"])) if "callout" in spec else None,
                 ("fringe", _xlist(spec["rd"])) if "rd" in spec else None,
                 ("justification", str(spec["justify"])) if "justify" in spec else None,
                 ("symbol", spec["symbol"]) if "symbol" in spec else None,
                 ("style", "cloudy") if spec.get("cloud") else None,
                 ("intensity", _xnum(spec["cloud"])) if spec.get("cloud") else None,
                 ("overlay-text", spec["overlay"]) if "overlay" in spec else None,
                 ("repeat", "true" if spec.get("repeat") else "false") if "overlay" in spec else None,
                 ("statemodel", spec["state_model"]) if "state_model" in spec else None,
                 ("state", spec["state"]) if "state_model" in spec else None]
        if "line" in spec:
            attrs += [("start", _xlist(spec["line"][:2])), ("end", _xlist(spec["line"][2:]))]
        if "endings" in spec:
            if spec["subtype"] == "FreeText":
                attrs.append(("head", spec["endings"][0]))
            else:
                attrs += [("head", spec["endings"][0]), ("tail", spec["endings"][1])]
        if "irt" in spec:
            attrs.append(("inreplyto", spec["irt"]["nm"]))
        if "irt_missing" in spec:
            attrs.append(("inreplyto", missing_parent_nm))
        if spec.get("rt") == "Group":
            attrs.append(("replyType", "group"))
        if "attachment" in spec:
            attrs += [("file", spec["attachment"]["name"]),
                      ("description", spec["attachment"].get("desc", ""))]
        if "sound" in spec:
            s = spec["sound"]
            attrs += [("bits", str(s["bits"])), ("channels", str(s["channels"])),
                      ("encoding", "Signed"), ("rate", str(s["rate"]))]
        element = XFDF_ELEMENT[spec["subtype"]]
        head = "<%s %s>" % (element, " ".join("%s=%s" % (k, quoteattr(v))
                                              for k, v in (a for a in attrs if a)))
        lines.append(head)
        if "contents" in spec and "rich" not in spec:
            lines.append("<contents>%s</contents>" % escape(spec["contents"]))
        if "rich" in spec:
            lines.append("<contents>%s</contents>" % escape(spec["contents"]))
            lines.append("<contents-richtext>%s</contents-richtext>"
                         % (XHTML % escape(spec["rich"])).split("?>", 1)[1])
        if "da" in spec:
            lines.append("<defaultappearance>%s</defaultappearance>" % escape(spec["da"]))
        if "ds" in spec:
            lines.append("<defaultstyle>%s</defaultstyle>" % escape(spec["ds"]))
        if "vertices" in spec:
            v = spec["vertices"]
            lines.append("<vertices>%s</vertices>" % ";".join(
                "%s,%s" % (_xnum(v[i]), _xnum(v[i + 1])) for i in range(0, len(v), 2)))
        if "ink" in spec:
            lines.append("<inklist>")
            for stroke in spec["ink"]:
                lines.append("<gesture>%s</gesture>" % ";".join(
                    "%s,%s" % (_xnum(stroke[i]), _xnum(stroke[i + 1]))
                    for i in range(0, len(stroke), 2)))
            lines.append("</inklist>")
        if "attachment" in spec:
            data = spec["attachment"]["data"]
            lines.append('<data MODE="raw" encoding="hex" length="%d">%s</data>'
                         % (len(data), data.hex().upper()))
        if "sound" in spec:
            data = spec["sound"]["data"]
            lines.append('<data MODE="raw" encoding="hex" length="%d">%s</data>'
                         % (len(data), data.hex().upper()))
        if "popup" in spec:
            popup = spec["popup"]
            lines.append('<popup flags=%s open=%s page="%d" rect=%s/>' % (
                quoteattr(_xfdf_flags(popup["flags"])), quoteattr("yes" if popup["open"] else "no"),
                fixture.pages.index(popup["page"]), quoteattr(_xlist(popup["rect"]))))
        lines.append("</%s>" % element)
    lines += ["</annots>", '<f href=%s/>' % quoteattr(fixture.name),
              '<ids original="%s" modified="%s"/>' % (fixture.file_id, fixture.file_id), "</xfdf>"]
    return ("\n".join(lines) + "\n").encode("utf-8")


# ---------------------------------------------------------------------------------------------
# Shared page furniture
# ---------------------------------------------------------------------------------------------

YELLOW = (1, 0.92, 0.23)
BLUE = (0.13, 0.4, 0.86)
RED = (0.86, 0.15, 0.15)
GREEN = (0.13, 0.55, 0.25)
PURPLE = (0.5, 0.25, 0.7)
ORANGE = (0.95, 0.55, 0.1)
NOTE_YELLOW = (1, 0.85, 0.3)
HELV_DA = "0 0 0 rg /Helv 10 Tf"
HELV_DS = "font: Helvetica,sans-serif 10.0pt; text-align:left; color:#000000"


def caption(page, x, y, text):
    page.text(x, y, 7, text)


def heading(page, text, media=pb.A4):
    page.text(48, media[1] - 50, 13, text, bold=True)


# ---------------------------------------------------------------------------------------------
# The fixtures
# ---------------------------------------------------------------------------------------------

FILES = {}


def generator(name):
    def register(function):
        FILES[name] = function
        return function
    return register


MARKUP_LINES = [
    ("Highlight", "This line carries a highlight over the words highlighted phrase here."),
    ("Underline", "This line carries an underline beneath the words underlined phrase here."),
    ("StrikeOut", "This line carries a strikeout through the words struck phrase here."),
    ("Squiggly", "This line carries a squiggly under the words squiggly phrase here."),
    ("Caret", "A caret marks an insertion point between these two words."),
    ("Redact", "A redaction mark covers the account number 4417 1234 5678 9113 here."),
]
MARKUP_PHRASES = {"Highlight": "highlighted phrase", "Underline": "underlined phrase",
                  "StrikeOut": "struck phrase", "Squiggly": "squiggly phrase",
                  "Redact": "4417 1234 5678 9113"}


def every_type_fixture():
    """One of every subtype Stage 6 writes, each with an /AP this generator drew. Built as a
    function rather than inline because the two exchange files describe exactly this document."""
    f = Fixture("annots-every-type.pdf", "Every annotation type", "Imani Okoye",
                "Stage 6 annotation fixture", b"D:20260920090000+08'00'")
    f.claims = ["one of every subtype Stage 6 writes, each with an appearance this generator drew",
                "a note with its pop-up; FreeText as a box, a callout and a typewriter",
                "a polygon with a cloudy border; a line with two different heads",
                "a file attachment and a sound, each with its payload; a redaction mark with /RO"]
    p = f.page()
    heading(p, "annots-every-type.pdf - one of every Stage 6 annotation type")
    author = "Imani Okoye"
    created = Date(20, 9, 0)
    serial_date = [0]

    def when():
        serial_date[0] += 1
        return Date(20, 9, serial_date[0])

    def common(subject):
        return {"author": author, "subject": subject, "created": created, "modified": when()}

    y = 760
    for kind, line in MARKUP_LINES:
        p.text(48, y, 11, line)
        if kind in MARKUP_PHRASES:
            x0, y0, x1, y1 = phrase_box(48, y, 11, line, MARKUP_PHRASES[kind])
            q = quad(x0, y0, x1, y1)
            fields = common(kind)
            fields.update(color={"Highlight": YELLOW, "Underline": BLUE, "StrikeOut": RED,
                                 "Squiggly": GREEN, "Redact": RED}[kind], quads=q,
                          contents="%s on '%s'" % (kind.lower(), MARKUP_PHRASES[kind]))
            if kind == "Redact":
                fields.update(interior=(0, 0, 0), overlay="REDACTED", repeat=False,
                              da="1 1 1 rg /Helv 9 Tf", contents="account number")
            if kind == "Highlight":
                fields["opacity"] = 0.6
            f.annot(p, kind, (x0, y0, x1, y1), **fields)
        else:
            before = width("A caret marks an insertion point between these two", 11)
            gap = 48 + before + width(" ", 11) / 2
            fields = common("Inserted Text")
            fields.update(color=BLUE, contents="new", symbol="None", rd=[0, 0, 0, 0])
            f.annot(p, "Caret", (gap - 3, y - 3, gap + 3, y + 5), **fields)
        y -= 22

    # Shapes, row 1
    f.annot(p, "Square", (48, 470, 168, 560), color=RED, interior=(0.99, 0.9, 0.9), border=2,
            contents="a square, filled", opacity=0.9, **common("Rectangle"))
    caption(p, 48, 460, "Square")
    f.annot(p, "Circle", (190, 470, 310, 560), color=BLUE, border=2, contents="a circle",
            **common("Oval"))
    caption(p, 190, 460, "Circle")
    f.annot(p, "Line", (330, 470, 547, 560), line=[340, 480, 537, 550],
            endings=("OpenArrow", "ClosedArrow"), color=GREEN, interior=GREEN, border=2,
            contents="a line with an open and a closed head", **common("Arrow"))
    caption(p, 330, 460, "Line, OpenArrow to ClosedArrow")

    # Row 2
    poly = [60, 360, 150, 360, 165, 410, 105, 445, 45, 410]
    f.annot(p, "Polygon", bounds(poly, 2), vertices=poly, color=PURPLE, border=1.5,
            contents="a pentagon", **common("Polygon"))
    caption(p, 48, 340, "Polygon")
    cloud_poly = [205, 365, 300, 365, 300, 435, 205, 435]
    f.annot(p, "Polygon", bounds(cloud_poly, 8), vertices=cloud_poly, cloud=1, color=ORANGE,
            border=1, rd=[8, 8, 8, 8], contents="a cloud", **common("Cloud"))
    caption(p, 190, 340, "Polygon, cloudy border")
    polyline = [340, 365, 390, 430, 440, 375, 490, 430, 537, 370]
    f.annot(p, "PolyLine", bounds(polyline, 2), vertices=polyline, color=BLUE, border=1.5,
            endings=("None", "OpenArrow"), contents="a polyline", **common("Polygonal Line"))
    caption(p, 330, 340, "PolyLine")

    # Row 3
    strokes = []
    for s in range(3):
        stroke = []
        for i in range(14):
            stroke += [56 + i * 7.5, 240 + s * 26 + ((i * 5 + s * 3) % 9) - 4 + (i % 3) * 2]
        strokes.append(stroke)
    pts = [v for stroke in strokes for v in stroke]
    f.annot(p, "Ink", bounds(pts, 2), ink=strokes, color=(0.1, 0.1, 0.5), border=2,
            contents="three ink strokes", **common("Pencil"))
    caption(p, 48, 220, "Ink")
    f.annot(p, "Stamp", (190, 255, 340, 299), icon="Approved", contents="Approved",
            **common("Approved"))
    caption(p, 190, 245, "Stamp /Approved")
    f.annot(p, "FileAttachment", (360, 270, 376, 294), icon="PushPin", color=BLUE,
            attachment={"name": "data.csv", "mime": "text/csv", "desc": "the figures behind table 2",
                        "data": b"quarter,cases\nQ1,12\nQ2,18\nQ3,24\n",
                        "modified": Date(19, 17, 30)},
            contents="data.csv", **common("File Attachment"))
    caption(p, 352, 245, "FileAttachment")
    f.annot(p, "Sound", (420, 272, 440, 292), icon="Speaker", color=NOTE_YELLOW,
            sound={"rate": 8000, "channels": 1, "bits": 16, "data": pcm_triangle()},
            contents="a quarter-second tone", **common("Sound"))
    caption(p, 412, 245, "Sound")
    note = f.annot(p, "Text", (480, 274, 500, 294), icon="Comment", color=NOTE_YELLOW,
                   contents="A sticky note with a pop-up.", **common("Sticky Note"))
    f.popup(note, (395, 170, 547, 250))
    caption(p, 480, 245, "Text + Popup")

    # Row 4: the three FreeText intents
    f.annot(p, "FreeText", (48, 80, 200, 150), color=(0.3, 0.3, 0.3), border=1,
            contents="A free text box in Helvetica, wrapped by the generator.", da=HELV_DA,
            ds=HELV_DS, rich="A free text box in Helvetica, wrapped by the generator.",
            justify=0, **common("Text Box"))
    caption(p, 48, 70, "FreeText box")
    callout_rect = (220, 60, 380, 200)
    f.annot(p, "FreeText", callout_rect, intent="FreeTextCallout", color=RED, border=1,
            callout=[250, 190, 260, 150, 280, 120], endings=("OpenArrow",),
            rd=[60, 0, 0, 80], contents="A callout pointing up.", da=HELV_DA, ds=HELV_DS,
            justify=0, **common("Callout"))
    caption(p, 220, 50, "FreeText callout")
    f.annot(p, "FreeText", (400, 110, 547, 130), intent="FreeTextTypeWriter",
            contents="Typewriter text.", da=HELV_DA, ds=HELV_DS, border=0, justify=0,
            **common("Typewriter"))
    caption(p, 400, 100, "FreeText typewriter")
    return f


@generator("annots-every-type.pdf")
def annots_every_type():
    f = every_type_fixture()
    data = f.build()
    return data, f.label()


def every_type_exchange():
    f = every_type_fixture()
    f.build()
    return fdf_bytes(f), xfdf_bytes(f)


@generator("annots-no-ap.pdf")
def annots_no_ap():
    """The markup types WITHOUT /AP on page 1 and the same annotations WITH ours on page 2: what a
    reader must generate for itself, and the pair D6-6's PDFium control compares (guide 3.3)."""
    f = Fixture("annots-no-ap.pdf", "Annotations without appearances", "Tomas Varga",
                "Stage 6 annotation fixture", b"D:20260921090000+02'00'")
    f.claims = ["page 1: nine types with NO /AP -- highlight, underline, strikeout, squiggly, "
                "square, circle, line, ink and a note",
                "page 2: the same nine, the same geometry, each WITH an /AP this generator drew"]
    for index, has_ap in enumerate((False, True)):
        p = f.page()
        heading(p, "annots-no-ap.pdf - page %d, %s" % (index + 1, "appearances drawn by the "
                                                       "generator" if has_ap else "NO /AP"))
        tag = "with appearance" if has_ap else "no appearance"
        y = 760
        for kind in ("Highlight", "Underline", "StrikeOut", "Squiggly"):
            line = "A %s line: the words marked phrase are the ones marked." % kind.lower()
            p.text(48, y, 11, line)
            x0, y0, x1, y1 = phrase_box(48, y, 11, line, "marked phrase")
            f.annot(p, kind, (x0, y0, x1, y1), quads=quad(x0, y0, x1, y1),
                    color={"Highlight": YELLOW, "Underline": BLUE, "StrikeOut": RED,
                           "Squiggly": GREEN}[kind], ap=has_ap, author="Tomas Varga",
                    contents="%s: %s" % (tag, kind), created=Date(21, 9, 0, (2, 0)))
            y -= 24
        f.annot(p, "Square", (48, 520, 168, 610), color=RED, border=2, ap=has_ap,
                author="Tomas Varga", contents="%s: Square" % tag)
        f.annot(p, "Circle", (190, 520, 310, 610), color=BLUE, border=2, ap=has_ap,
                author="Tomas Varga", contents="%s: Circle" % tag)
        f.annot(p, "Line", (330, 520, 540, 610), line=[340, 530, 530, 600], color=GREEN,
                border=2, ap=has_ap, author="Tomas Varga", contents="%s: Line" % tag)
        stroke = []
        for i in range(16):
            stroke += [60 + i * 9, 430 + ((i * 7) % 11) * 3]
        f.annot(p, "Ink", bounds(stroke, 2), ink=[stroke], color=(0.1, 0.1, 0.5), border=2,
                ap=has_ap, author="Tomas Varga", contents="%s: Ink" % tag)
        f.annot(p, "Text", (330, 440, 350, 460), icon="Comment", color=NOTE_YELLOW, ap=has_ap,
                author="Tomas Varga", contents="%s: Text" % tag)
    data = f.build()
    return data, f.label()


THREAD_AUTHORS = ["Ana Lima", "Ben Okafor", "Zoë Müller"]


def threads_fixture():
    f = Fixture("annots-threads.pdf", "Review threads", "Ana Lima",
                "Stage 6 annotation fixture", b"D:20260930180000-03'00'")
    f.claims = ["three authors (one with a non-ASCII name), replies to replies, a group "
                "(/RT /Group), Review and Marked state replies with a history, dates across "
                "September 2026 in four time zones",
                "the controls: an /IRT cycle, a reply whose parent object does not exist, a reply "
                "on page 2 to a note on page 1"]
    ana, ben, zoe = THREAD_AUTHORS
    p1 = f.page()
    p2 = f.page()
    heading(p1, "annots-threads.pdf - page 1, review threads")
    heading(p2, "annots-threads.pdf - page 2, a reply from across the page break")
    p1.text(48, 760, 11, "The quarterly figures below were checked against the source tables.")
    p1.text(48, 740, 11, "Each reviewer marks what they have read and sets a review status.")
    p1.text(48, 720, 11, "The renderer throughput rose by eleven percent over the quarter.")
    p2.text(48, 760, 11, "Page two continues the discussion from the first page.")

    def note(page, rect, author, text, date, **extra):
        spec = f.annot(page, "Text", rect, icon="Comment", color=NOTE_YELLOW, author=author,
                       contents=text, rich=text, created=date, modified=date,
                       subject="Sticky Note", **extra)
        return spec

    def reply_rect(parent):
        """A reply's icon: the parent's own rectangle when the parent is a note, otherwise a note-
        sized square in the right margin at the parent's height, clear of every line of text."""
        if parent["subtype"] == "Text":
            return parent["rect"]
        y0 = parent["rect"][1]
        return (540, y0 - 4, 560, y0 + 16)

    def reply(parent, author, text, date):
        return f.annot(parent["page"], "Text", reply_rect(parent), icon="Comment",
                       color=NOTE_YELLOW,
                       author=author, contents=text, created=date, modified=date, irt=parent,
                       rt="R", subject="Sticky Note")

    def status(parent, author, model, state, date):
        return f.annot(parent["page"], "Text", reply_rect(parent), icon="Comment", author=author,
                       contents="%s set by %s" % (state, author), created=date, modified=date,
                       irt=parent, state_model=model, state=state, flags=HIDDEN_STATE,
                       subject="Sticky Note", ap=False)

    a = note(p1, (500, 752, 520, 772), ana, "Is this figure current?", Date(1, 9, 15, (8, 0)))
    f.popup(a, (380, 640, 560, 740))
    b = reply(a, ben, "Updated last week from the source.", Date(3, 14, 2, (1, 0)))
    reply(b, zoe, "Confirmed against the source table.", Date(5, 8, 40, None))
    reply(a, ana, "Thanks, both of you.", Date(6, 18, 5, (-5, 0)))
    status(a, ben, "Review", "Accepted", Date(8, 10, 0, (1, 0)))
    status(a, zoe, "Review", "Rejected", Date(12, 11, 30, None))
    status(a, ben, "Review", "Completed", Date(20, 16, 45, (1, 0)))
    status(a, ana, "Marked", "Marked", Date(10, 9, 0, (8, 0)))

    line = "The renderer throughput rose by eleven percent over the quarter."
    x0, y0, x1, y1 = phrase_box(48, 720, 11, line, "eleven percent")
    h = f.annot(p1, "Highlight", (x0, y0, x1, y1), quads=quad(x0, y0, x1, y1), color=YELLOW,
                author=zoe, contents="Source for this number?", created=Date(2, 7, 30, None),
                modified=Date(2, 7, 30, None), subject="Highlight")
    reply(h, ana, "Table 4 of the engine report.", Date(4, 12, 0, (8, 0)))
    status(h, ana, "Review", "Cancelled", Date(25, 9, 30, (8, 0)))
    status(h, zoe, "Marked", "Marked", Date(26, 10, 0, None))
    status(h, zoe, "Marked", "Unmarked", Date(27, 10, 0, None))

    # A replace edit as Acrobat writes it: the caret holds the new text, the strikeout over the old
    # text is in its group (/IRT the caret, /RT /Group) and is shown as one item with it.
    old_line = "Each reviewer marks what they have read and sets a review status."
    sx0, sy0, sx1, sy1 = phrase_box(48, 740, 11, old_line, "review status")
    caret = f.annot(p1, "Caret", (sx1 - 2, sy0 - 2, sx1 + 6, sy0 + 8), color=BLUE, author=ben,
                    contents="decision", created=Date(9, 13, 0, (1, 0)),
                    modified=Date(9, 13, 0, (1, 0)), symbol="None", subject="Replace Text")
    f.annot(p1, "StrikeOut", (sx0, sy0, sx1, sy1), quads=quad(sx0, sy0, sx1, sy1), color=BLUE,
            author=ben, created=Date(9, 13, 0, (1, 0)), modified=Date(9, 13, 0, (1, 0)),
            irt=caret, rt="Group", subject="Replace Text")

    # The cycle: two notes, each in reply to the other.
    x = note(p1, (500, 600, 520, 620), ben, "Which of these came first?", Date(14, 9, 0, (1, 0)))
    y = note(p1, (530, 600, 550, 620), zoe, "Neither, apparently.", Date(14, 9, 5, None))
    x["irt"] = y
    y["irt"] = x
    x["rt"] = y["rt"] = "R"
    # A reply whose parent is gone: object 999 is not in the file.
    note(p1, (500, 560, 520, 580), ana, "Replying to a deleted comment.", Date(16, 10, 0, (8, 0)),
         irt_missing=999, rt="R")
    # A reply on page 2 to the note on page 1.
    reply_across = f.annot(p2, "Text", (500, 752, 520, 772), icon="Comment", color=NOTE_YELLOW,
                           author=zoe, contents="Answering the page 1 note from here.",
                           created=Date(30, 17, 0, None), modified=Date(30, 17, 0, None),
                           irt=a, rt="R", subject="Sticky Note")
    e = note(p2, (500, 700, 520, 720), ben, "A thread of its own on page 2.", Date(28, 8, 0, (1, 0)))
    f.popup(e, (380, 590, 560, 690))
    reply(e, ana, "Agreed.", Date(29, 8, 30, (8, 0)))
    return f, {"a": a, "h": h, "caret": caret, "x": x, "y": y, "across": reply_across}


@generator("annots-threads.pdf")
def annots_threads():
    f, named = threads_fixture()
    data = f.build()

    def nm(spec):
        return spec["nm"]

    def history(parent, model):
        replies = [s for s in f.specs if s.get("irt") is parent and s.get("state_model") == model]
        return [{"author": s["author"], "state": s["state"], "createdUtc": s["created"].utc,
                 "object": s["obj"]} for s in sorted(replies, key=lambda s: s["created"].utc)]

    def children(parent):
        return [s["obj"] for s in f.specs if s.get("irt") is parent and "state_model" not in s
                and s.get("rt") == "R"]
    tops = [s for s in f.specs if s["subtype"] != "Popup" and "irt" not in s
            and "irt_missing" not in s]
    threads = {str(s["obj"]): children(s) for s in f.specs
               if s["subtype"] != "Popup" and children(s)}
    label = f.label(
        authors=sorted(THREAD_AUTHORS),
        topLevel=[s["obj"] for s in tops],
        replies=threads,
        reviewHistory={str(named["a"]["obj"]): history(named["a"], "Review"),
                       str(named["h"]["obj"]): history(named["h"], "Review")},
        markedHistory={str(named["a"]["obj"]): history(named["a"], "Marked"),
                       str(named["h"]["obj"]): history(named["h"], "Marked")},
        groups=[{"parent": named["caret"]["obj"],
                 "members": [s["obj"] for s in f.specs if s.get("irt") is named["caret"]
                             and s.get("rt") == "Group"]}],
        controls={"cycle": [named["x"]["obj"], named["y"]["obj"]],
                  "missingParent": [s["obj"] for s in f.specs if "irt_missing" in s],
                  "crossPage": named["across"]["obj"]},
        note="topLevel excludes pop-ups and every annotation with an /IRT, the cycle's two "
             "included -- a cycle has no top, which the reader must report and then show both as "
             "top-level (plan D6-12). replies maps a parent's object to its /RT /R children, "
             "state replies excluded.")
    return data, label


def threads_exchange():
    f, _ = threads_fixture()
    f.build()
    return fdf_bytes(f), xfdf_bytes(f)


FLAG_CASES = [("Locked", "Square"), ("LockedContents", "FreeText"), ("ReadOnly", "Square"),
              ("Hidden", "Square"), ("NoView", "Square"), ("NoZoom", "Text"),
              ("NoRotate", "Text")]


@generator("annots-flags.pdf")
def annots_flags():
    """Each flag D6-27 honours, on an annotation beside an UNFLAGGED twin -- so a reader that
    ignores the flag and one that honours it give visibly different answers."""
    f = Fixture("annots-flags.pdf", "Annotation flags", "Kofi Mensah",
                "Stage 6 annotation fixture", b"D:20260922090000Z")
    f.claims = ["Locked, LockedContents, ReadOnly, Hidden, NoView, NoZoom and NoRotate, each on "
                "its own annotation with an unflagged twin to its right"]
    p = f.page()
    heading(p, "annots-flags.pdf - flagged (left) and unflagged twin (right)")
    y = 740
    for flag, kind in FLAG_CASES:
        p.text(48, y + 8, 10, flag)
        for column, flagged in ((180, True), (380, False)):
            base = NOTE_FLAGS if kind == "Text" else PRINT
            flags = base | FLAGS[flag] if flagged else base
            if kind == "Text" and flag in ("NoZoom", "NoRotate"):
                flags = (PRINT | FLAGS[flag]) if flagged else PRINT
            fields = {"flags": flags, "author": "Kofi Mensah",
                      "contents": "%s %s" % (flag, "set" if flagged else "twin, no flag")}
            if kind == "Square":
                f.annot(p, "Square", (column, y, column + 120, y + 40), color=RED,
                        interior=(0.99, 0.9, 0.9), border=2, **fields)
            elif kind == "FreeText":
                fields["contents"] = "LockedContents text" if flagged else "Editable text"
                f.annot(p, "FreeText", (column, y, column + 120, y + 40), color=(0.3, 0.3, 0.3),
                        border=1, da=HELV_DA, ds=HELV_DS, justify=0, **fields)
            else:
                f.annot(p, "Text", (column, y + 10, column + 20, y + 30), icon="Comment",
                        color=NOTE_YELLOW, **fields)
        y -= 80
    data = f.build()
    return data, f.label()


@generator("annots-rotated.pdf")
def annots_rotated():
    """The same content and annotations on pages with /Rotate 0, 90, 180 and 270. The note is
    NoRotate (Acrobat's default for one), and the free text carries /Rotate and an appearance
    /Matrix so it reads upright -- the way Acrobat writes a text box on a turned page."""
    f = Fixture("annots-rotated.pdf", "Annotations on rotated pages", "Leila Haddad",
                "Stage 6 annotation fixture", b"D:20260923090000+04'00'")
    f.claims = ["pages with /Rotate 0, 90, 180, 270, identical content and annotations in page "
                "space; page 1 (rotation 0) is the control",
                "the note is NoZoom and NoRotate; the free text has /Rotate equal to its page's and "
                "an appearance /Matrix"]
    line = "Marked words on a rotated page: these words are highlighted."
    for rotate in (0, 90, 180, 270):
        p = f.page(rotate=rotate)
        heading(p, "annots-rotated.pdf - /Rotate %d" % rotate)
        p.text(48, 740, 11, line)
        x0, y0, x1, y1 = phrase_box(48, 740, 11, line, "these words")
        f.annot(p, "Highlight", (x0, y0, x1, y1), quads=quad(x0, y0, x1, y1), color=YELLOW,
                author="Leila Haddad", contents="highlight at /Rotate %d" % rotate)
        f.annot(p, "Square", (60, 560, 200, 660), color=RED, border=2, author="Leila Haddad",
                contents="square at /Rotate %d" % rotate)
        f.annot(p, "Text", (400, 620, 420, 640), icon="Comment", color=NOTE_YELLOW,
                author="Leila Haddad", contents="note at /Rotate %d" % rotate)
        w, h = 160, 40
        rect = (250, 420, 250 + w, 420 + h) if rotate in (0, 180) else (250, 420, 250 + h,
                                                                        420 + w)
        f.annot(p, "FreeText", rect, rotate=rotate, color=(0.3, 0.3, 0.3), border=1,
                contents="Upright at /Rotate %d" % rotate, da=HELV_DA, ds=HELV_DS, justify=0,
                author="Leila Haddad")
    data = f.build()
    return data, f.label()


@generator("annots-direct.pdf")
def annots_direct():
    """Direct annotation dictionaries, an annotation listed by two pages, one with no /P -- three
    things legal or common in real files and wrong in any reader that assumes an /Annots entry is a
    reference to a dictionary owned by the page that lists it."""
    f = Fixture("annots-direct.pdf", "Direct and shared annotations", "Priya Raman",
                "Stage 6 annotation fixture", b"D:20260924090000+05'30'")
    f.claims = ["page 1: two DIRECT annotation dictionaries in /Annots (one with no /NM) beside an "
                "indirect one",
                "an indirect Square listed by BOTH page 1 and page 2, its /P naming page 1",
                "page 2: a Circle with no /P",
                "an annotation with no /P that NO page lists, reached only through the /IRT of a "
                "hidden state reply on page 2 -- unplaceable, so a reader reports it (D6-3)",
                "page 3: the control, every /Annots entry an indirect reference with /P"]
    p1, p2, p3 = f.page(), f.page(), f.page()
    for index, p in enumerate((p1, p2, p3)):
        heading(p, "annots-direct.pdf - page %d" % (index + 1))
    f.annot(p1, "Square", (60, 600, 180, 680), color=RED, border=2, direct=True,
            author="Priya Raman", contents="a direct dictionary")
    f.annot(p1, "Text", (220, 640, 240, 660), icon="Comment", color=NOTE_YELLOW, direct=True,
            nm=None, author="Priya Raman", contents="direct, and no /NM")
    f.annot(p1, "Circle", (280, 600, 400, 680), color=BLUE, border=2, author="Priya Raman",
            contents="an ordinary indirect annotation")
    shared = f.annot(p1, "Square", (60, 400, 180, 480), color=GREEN, border=2,
                     author="Priya Raman", contents="listed by pages 1 and 2")
    p2.listing.append(shared)
    f.annot(p2, "Circle", (280, 400, 400, 480), color=PURPLE, border=2, no_p=True,
            author="Priya Raman", contents="no /P")
    # Unplaceable: written, never listed, no /P. A hidden Marked state reply on page 2 points at it,
    # which is how a reader comes to meet it at all -- and being hidden, neither draws anything.
    orphan = f.annot(p2, "Square", (60, 200, 180, 280), color=RED, border=2, no_p=True,
                     unlisted=True, author="Priya Raman", contents="listed by no page")
    f.annot(p2, "Text", (60, 200, 80, 220), icon="Comment", author="Priya Raman",
            contents="Marked set by Priya Raman", irt=orphan, state_model="Marked",
            state="Marked", flags=HIDDEN_STATE, ap=False)
    f.annot(p3, "Square", (60, 600, 180, 680), color=RED, border=2, author="Priya Raman",
            contents="control: indirect")
    f.annot(p3, "Text", (220, 640, 240, 660), icon="Comment", color=NOTE_YELLOW,
            author="Priya Raman", contents="control: indirect note")
    data = f.build()
    return data, f.label(note="the shared Square appears in both pages' annots lists with the "
                              "same object number; listedBy names both. unlisted holds the "
                              "annotation no page lists")


@generator("annots-unknown-keys.pdf")
def annots_unknown_keys():
    """Private keys an editor does not model, and rich text -- what D6-5's preservation case
    compares byte for byte before and after an edit. Page 2 is the same without them."""
    f = Fixture("annots-unknown-keys.pdf", "Unmodelled annotation keys", "Mateo Rossi",
                "Stage 6 annotation fixture", b"D:20260925090000+02'00'")
    f.claims = ["page 1: /PieceInfo (a private dictionary), /RawPageTestKey in three value types, "
                "and /RC rich text",
                "page 2: the same annotations with none of them"]
    piece = (b"<< /RawPageCorpus << /LastModified (D:20260925090000+02'00') "
             b"/Private << /Version 3 /Tags [ /alpha /beta ] >> >> >>")
    for index, with_keys in enumerate((True, False)):
        p = f.page()
        heading(p, "annots-unknown-keys.pdf - page %d, %s" % (
            index + 1, "private keys and rich text" if with_keys else "the control, none"))
        extra_square = [(b"/PieceInfo", piece), (b"/RawPageTestKey", b"(kept verbatim)")]
        extra_note = [(b"/RawPageTestKey", b"[ 1 2.5 /Three (four) ]")]
        extra_text = [(b"/RawPageTestKey", b"<< /Nested << /Deeper true >> /Count 2 >>")]
        f.annot(p, "Square", (60, 600, 200, 700), color=RED, border=2, author="Mateo Rossi",
                contents="square with a private dictionary",
                extra=extra_square if with_keys else [])
        rich = {"rich": "Rich text: <b> is escaped, not bold."} if with_keys else {}
        f.annot(p, "Text", (240, 660, 260, 680), icon="Comment", color=NOTE_YELLOW,
                author="Mateo Rossi", contents="Rich text: <b> is escaped, not bold.",
                extra=extra_note if with_keys else [], **rich)
        f.annot(p, "FreeText", (300, 600, 500, 660), color=(0.3, 0.3, 0.3), border=1,
                author="Mateo Rossi", contents="Free text carrying a nested private key.",
                da=HELV_DA, ds=HELV_DS, justify=0,
                extra=extra_text if with_keys else [],
                **({"rich": "Free text carrying a nested private key."} if with_keys else {}))
    data = f.build()
    return data, f.label()


ANNOTS_900_KINDS = ["Highlight", "Underline", "StrikeOut", "Squiggly", "Square", "Circle", "Line",
                    "Ink", "Text", "FreeText"]
ANNOTS_900_AUTHORS = ["Ana Lima", "Ben Okafor", "Imani Okoye", "Kofi Mensah"]


@generator("annots-900.pdf")
def annots_900():
    """900 pages, ten annotations each -- 9,000 -- the workload the Comments pane is measured on
    (guide 3.6). The appearance streams are SHARED (one per kind and author colour) and the
    dictionaries packed into an object stream, so the file stays small."""
    f = Fixture("annots-900.pdf", "Nine thousand annotations", "Ben Okafor",
                "Stage 6 annotation fixture", b"D:20260926090000+01'00'")
    f.claims = ["900 pages, ten annotations a page (9,000) in a fixed order of kinds, every "
                "appearance stream shared between pages, the dictionaries in object streams"]
    line = "Page marker line with words to mark for the annotation workload."
    colours = [YELLOW, BLUE, RED, GREEN]
    for number in range(900):
        p = f.page()
        p.text(48, 800, 9, "annots-900.pdf - page %d of 900" % (number + 1))
        p.text(48, 760, 11, line)
        x0, y0, x1, y1 = phrase_box(48, 760, 11, line, "words to mark")
        for k, kind in enumerate(ANNOTS_900_KINDS):
            serial = number * 10 + k
            author = ANNOTS_900_AUTHORS[serial % 4]
            fields = {"author": author, "contents": "%s %d on page %d" % (kind, k + 1, number + 1),
                      "color": colours[serial % 4], "share_ap": True,
                      "created": Date(1 + number % 28, 9 + k, (number * 7) % 60, (1, 0)),
                      "subject": kind}
            band = 700 - k * 60
            if kind in ("Highlight", "Underline", "StrikeOut", "Squiggly"):
                fields["color"] = colours[k % 4]
                f.annot(p, kind, (x0, y0, x1, y1), quads=quad(x0, y0, x1, y1), **fields)
            elif kind in ("Square", "Circle"):
                f.annot(p, kind, (60, band - 40, 180, band), border=1, **fields)
            elif kind == "Line":
                f.annot(p, kind, (60, band - 40, 180, band), line=[62, band - 38, 178, band - 2],
                        border=1, **fields)
            elif kind == "Ink":
                f.annot(p, kind, (60, band - 40, 180, band),
                        ink=[[62, band - 38, 120, band - 2, 178, band - 38]], border=1, **fields)
            elif kind == "Text":
                f.annot(p, kind, (60, band - 20, 80, band), icon="Comment", **fields)
            else:
                f.annot(p, kind, (60, band - 40, 260, band), border=1, da=HELV_DA, justify=0,
                        ap_text="Free text on every page", **fields)
    data = f.build(objstm=True)
    full = json.loads(f.label().decode("utf-8"))
    label = {"file": full["file"], "generator": GENERATOR, "units": full["units"],
             "fileId": full["fileId"], "claims": full["claims"],
             "formula": {"pages": 900, "annotsPerPage": 10, "kinds": ANNOTS_900_KINDS,
                         "authors": ANNOTS_900_AUTHORS,
                         "author": "authors[(page * 10 + k) mod 4]",
                         "contents": "{kind} {k + 1} on page {page + 1}",
                         "created": "2026-09-(1 + page mod 28) at (9 + k):((page * 7) mod 60), "
                                    "+01'00'",
                         "geometry": "identical on every page: see pages 0, 1 and 899"},
             "totalAnnotations": 9000,
             "pages": [full["pages"][0], full["pages"][1], full["pages"][899]]}
    return data, (json.dumps(label, ensure_ascii=False, indent=1) + "\n").encode("utf-8")


ATTACHMENTS = [
    ("appendix.pdf", "application/pdf", "A one-page appendix, attached as a PDF"),
    ("readme.txt", "text/plain", "Plain-text notes about this document"),
    ("setup.exe", "application/octet-stream",
     "Named like an executable; its bytes are text and run nothing"),
]


def _appendix_pdf():
    pdf = pb.Pdf()
    font = pb.base14(pdf, "Helvetica")
    resources = pb.dictionary([(b"/Font", pb.dictionary([(b"/F1", pb.ref(font))]))])
    contents = pb.content_stream(pdf, pb.show_text(72, 720, 14, "Attached appendix page") + b"\n")
    catalog, _, _ = pb.simple_document(
        pdf, [lambda parent: pb.page(parent, (300.0, 300.0), contents, resources)])
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: attached appendix"))


@generator("attachments.pdf")
def attachments():
    """Three document-level files with descriptions and a FileAttachment annotation. The .exe is
    text: D6-17 refuses to OPEN an executable type by its extension, and the fixture must let that
    be tested without the corpus carrying a program."""
    f = Fixture("attachments.pdf", "Attachments", "Noa Levi", "Stage 6 attachment fixture",
                b"D:20260927090000+03'00'")
    f.claims = ["/Names /EmbeddedFiles with appendix.pdf, readme.txt and setup.exe, each with "
                "/Desc, /Subtype, /Params /Size and /CheckSum",
                "a FileAttachment annotation on page 1 carrying measurements.csv",
                "the control, a document with no attachment, is ordinary/text-a4.pdf"]
    payloads = {"appendix.pdf": _appendix_pdf(),
                "readme.txt": b"Notes for reviewers: the appendix holds the raw tables.\n",
                "setup.exe": b"This file is named like a program and is plain text.\n"}
    p = f.page()
    heading(p, "attachments.pdf - three attached files and one attachment annotation")
    p.text(48, 740, 11, "The document carries three files; the pin below carries a fourth.")
    f.annot(p, "FileAttachment", (100, 640, 116, 664), icon="PushPin", color=BLUE,
            author="Noa Levi", contents="measurements.csv",
            attachment={"name": "measurements.csv", "mime": "text/csv",
                        "desc": "the measurements behind figure 1",
                        "data": b"sample,length_mm\n1,12.5\n2,12.7\n3,12.4\n",
                        "modified": Date(26, 15, 0, (3, 0))})
    modified = Date(25, 12, 0, (3, 0))

    def files_tree(pdf):
        pairs = []
        for name, mime, desc in ATTACHMENTS:        # already in name order, as a name tree must be
            pairs += [pb.literal(name), pb.ref(file_spec(pdf, {
                "name": name, "mime": mime, "desc": desc, "data": payloads[name],
                "modified": modified}))]
        return [(b"/Names", pb.dictionary([(b"/EmbeddedFiles", pb.dictionary([
            (b"/Names", pb.array(pairs))]))]))]
    data = f.build(root_extra=files_tree)
    label = f.label(documentFiles=[
        {"name": name, "mime": mime, "description": desc, "size": len(payloads[name]),
         "sha256": hashlib.sha256(payloads[name]).hexdigest(), "modified": modified.raw}
        for name, mime, desc in ATTACHMENTS],
        executableNames=["setup.exe"])
    return data, label


MEASURE = {"ratio": "1 in = 4 ft",
           "x": {"unit": "ft", "factor": 0.0555556, "precision": 100},
           "distance": {"unit": "ft", "factor": 1, "precision": 100},
           "area": {"unit": "sq ft", "factor": 1, "precision": 100}}


@generator("measure-scale.pdf")
def measure_scale():
    """A drawing whose every endpoint, midpoint and intersection is known, at a stated scale -- 1 in
    on paper is 4 ft, so 72 pt is 4 ft -- and three measurement annotations over it with /Measure.
    Page 2 has no path at all: the control for snapping to objects (D6-14)."""
    f = Fixture("measure-scale.pdf", "Measurement at scale", "Kofi Mensah",
                "Stage 6 measurement fixture", b"D:20260928090000Z")
    f.claims = ["a 216 x 144 pt rectangle (12 ft x 8 ft at 1 in = 4 ft) with both diagonals, and a "
                "288 pt line crossed at its midpoint by a vertical one",
                "a distance (Line /LineDimension), a perimeter (PolyLine /PolyLineDimension) and an "
                "area (Polygon /PolygonDimension), each with /Measure /Subtype /RL",
                "page 2: text and no path, the control"]
    p = f.page()
    heading(p, "measure-scale.pdf - known geometry at 1 in = 4 ft")
    segments = [(100, 500, 316, 500), (316, 500, 316, 644), (316, 644, 100, 644),
                (100, 644, 100, 500), (100, 500, 316, 644), (100, 644, 316, 500),
                (100, 400, 388, 400), (244, 350, 244, 450)]
    p.ops.append(b"0 G 1 w " + b" ".join(
        pt(a, b) + b" m " + pt(c, d) + b" l S" for a, b, c, d in segments))
    points = []
    for a, b, c, d in segments:
        points += [{"kind": "endpoint", "x": a, "y": b}, {"kind": "endpoint", "x": c, "y": d},
                   {"kind": "midpoint", "x": r3((a + c) / 2), "y": r3((b + d) / 2)}]
    points += [{"kind": "intersection", "x": 208, "y": 572, "of": [4, 5]},
               {"kind": "intersection", "x": 244, "y": 400, "of": [6, 7]}]
    unique = []
    for point in points:
        if not any(u["x"] == point["x"] and u["y"] == point["y"] for u in unique):
            unique.append(point)
    p.text(100, 470, 9, "rectangle 12 ft x 8 ft; diagonals cross at its centre")
    p.text(100, 370, 9, "a 16 ft line crossed at its midpoint")
    f.annot(p, "Line", (95, 480, 321, 510), line=[100, 490, 316, 490], intent="LineDimension",
            leader=0, endings=("ClosedArrow", "ClosedArrow"), color=RED, interior=RED, border=1,
            measure=MEASURE, contents="12.00 ft", author="Kofi Mensah")
    perimeter = [100, 500, 100, 644, 316, 644, 316, 500]
    f.annot(p, "PolyLine", bounds(perimeter, 3), vertices=perimeter, intent="PolyLineDimension",
            color=BLUE, border=1, measure=MEASURE, contents="28.00 ft", author="Kofi Mensah")
    area = [100, 500, 316, 500, 316, 644, 100, 644]
    f.annot(p, "Polygon", bounds(area, 3), vertices=area, intent="PolygonDimension", color=GREEN,
            border=1, opacity=0.5, measure=MEASURE, contents="96.00 sq ft", author="Kofi Mensah")
    q = f.page()
    heading(q, "measure-scale.pdf - page 2, no paths: the control")
    q.text(48, 700, 11, "This page draws text only, so there is nothing to snap to.")
    data = f.build()
    return data, f.label(scale={"ratio": "1 in = 4 ft", "pointsPerUnit": 18,
                                "unit": "ft"},
                         geometry={"page": 0, "segments": [list(s) for s in segments],
                                   "points": unique},
                         expected={"distance": "216 pt = 12 ft", "perimeter": "504 pt = 28 ft",
                                   "area": "216 x 144 pt = 96 sq ft"})


# ---------------------------------------------------------------------------------------------
# Migration: report-v1, annotated, against report-v2
# ---------------------------------------------------------------------------------------------

def _report_lines(doc):
    """[(page index, [records])] as `make-compare.render_page` drew them."""
    out = []
    for index in range(len(doc["pages"])):
        records = []
        _COMPARE.render_page(doc, index, b"/Im1", records)
        out.append(records)
    return out


def _find_line(records, phrase):
    hits = [r for r in records if r["text"] and phrase in r["text"]
            and r["text"].count(phrase) == 1]
    return hits


@generator("annots-report-v1.pdf")
def annots_report_v1():
    """`compare/report-v1.pdf`'s pages, byte for byte in their content, carrying annotations whose
    fate in `compare/report-v2.pdf` is known: markup on text that survives (in place, on a moved
    paragraph, on a page that moved), markup on text v2 deleted, and notes beside a figure and a
    table. `labels/annots-migration.json` says where each must land, or that it must be REPORTED."""
    v1, v2, _, _ = _COMPARE.report()
    lines_v1 = _report_lines(v1)
    lines_v2 = _report_lines(v2)
    page_map = {0: 0, 1: 1, 2: 3, 3: 4}           # report-v2 inserted a page at index 2

    f = Fixture("annots-report-v1.pdf", "Engine report", "Kofi Mensah",
                "Stage 6 migration fixture", b"D:20260920100000Z")
    f.claims = ["compare/report-v1.pdf's content streams, unchanged, with annotations whose "
                "migration to compare/report-v2.pdf is labelled in labels/annots-migration.json"]
    pages = []
    for index in range(len(v1["pages"])):
        p = f.page()
        p.ops.append(_COMPARE.render_page(v1, index, b"/Im1").rstrip(b"\n"))
        pages.append(p)

    migration = []
    when = [0]

    def stamp():
        when[0] += 1
        return Date(21, 10, when[0], None)

    def page_text(records):
        return " ".join(r["text"] for r in records if r["text"])

    def markup(page_index, kind, phrase, colour, placeable, why):
        hits = _find_line(lines_v1[page_index], phrase)
        assert len(hits) == 1, (page_index, phrase, len(hits))
        line = hits[0]
        x0, y0, x1, y1 = phrase_box(line["x"], line["y"], line["size"], line["text"], phrase)
        spec = f.annot(pages[page_index], kind, (x0, y0, x1, y1), quads=quad(x0, y0, x1, y1),
                       color=colour, author="Kofi Mensah", contents=phrase, created=stamp())
        entry = {"nm": spec["nm"], "subtype": kind, "page": page_index, "text": phrase,
                 "placeable": placeable, "why": why}
        target = page_map[page_index]
        v2_hits = _find_line(lines_v2[target], phrase)
        if placeable:
            assert len(v2_hits) == 1, ("placeable phrase must occur once in v2", phrase)
            assert page_text(lines_v2[target]).count(phrase) == 1, phrase
            t = v2_hits[0]
            box2 = phrase_box(t["x"], t["y"], t["size"], t["text"], phrase)
            entry.update(expectedPage=target,
                         expectedQuadPoints=[r3(v) for v in quad(*box2)])
        else:
            for other in lines_v2:
                assert phrase not in page_text(other), ("unplaceable phrase found in v2", phrase)
        migration.append(entry)

    def note_beside(page_index, rect, contents, why):
        spec = f.annot(pages[page_index], "Text", rect, icon="Comment", color=NOTE_YELLOW,
                       author="Kofi Mensah", contents=contents, created=stamp())
        cx, cy = (rect[0] + rect[2]) / 2, (rect[1] + rect[3]) / 2
        texts = [r for r in lines_v1[page_index] if r["text"] and r["kind"] == "para"]

        def gap(r):
            # Distance from the note's centre to the line's BOX, then the higher line on a tie.
            x0, y0, x1, y1 = box(r["x"], r["y"], r["size"], r["text"])
            dx = max(x0 - cx, 0, cx - x1)
            dy = max(y0 - cy, 0, cy - y1)
            return (dx * dx + dy * dy, -r["y"])
        anchor = min(texts, key=gap)
        target = page_map[page_index]
        same = [r for r in lines_v2[target] if r["text"] == anchor["text"]]
        assert len(same) == 1, ("anchor line must occur once in v2", anchor["text"])
        dx, dy = same[0]["x"] - anchor["x"], same[0]["y"] - anchor["y"]
        migration.append({
            "nm": spec["nm"], "subtype": "Text", "page": page_index, "placeable": True, "why": why,
            "anchorRule": "the body line whose box is nearest the note's centre in v1 (the higher "
                          "on a tie); the note keeps its offset from that line's origin",
            "anchor": {"text": anchor["text"], "v1": [r3(anchor["x"]), r3(anchor["y"])],
                       "v2": [r3(same[0]["x"]), r3(same[0]["y"])]},
            "expectedPage": target,
            "expectedRect": [r3(rect[0] + dx), r3(rect[1] + dy), r3(rect[2] + dx),
                             r3(rect[3] + dy)]})

    def around(page_index, item, at, span=5):
        """`span` words of the drawn line holding word `at` of paragraph `item`, as centred on it as
        the line allows -- a phrase taken by word index alone can straddle a line break."""
        seen = 0
        for r in lines_v1[page_index]:
            if r["kind"] != "para" or r.get("item") != item:
                continue
            words_here = r["text"].split()
            if seen <= at < seen + len(words_here):
                local_at = at - seen
                start = max(0, min(local_at - span // 2, len(words_here) - span))
                return " ".join(words_here[start:start + span])
            seen += len(words_here)
        raise KeyError((page_index, item, at))

    def first_words(page_index, item, count, line=0):
        for r in lines_v1[page_index]:
            if r["kind"] == "para" and r.get("item") == item and r.get("line") == line:
                return " ".join(r["text"].split()[:count])
        raise KeyError((page_index, item))

    # Survives in place: page 1, the first body paragraph's first line.
    markup(0, "Highlight", first_words(0, 1, 4), YELLOW, True,
           "text unchanged, in the same place")
    # Survives on a MOVED paragraph: the last paragraph of page 1 moves up to third.
    moved = v1["pages"][0][8]["words"]
    markup(0, "Underline", " ".join(moved[1:5]), BLUE, True,
           "text unchanged, its paragraph moved up the page")
    # Deleted text: the word page 1 replaced, with its neighbours so the phrase is unique.
    markup(0, "StrikeOut", around(0, 2, 6), RED, False,
           "the middle word was replaced by 'throughput' in v2")
    # Survives on a page that MOVED: v1 page 3 is v2 page 4.
    markup(2, "Highlight", first_words(2, 1, 5), YELLOW, True,
           "text unchanged, its page now follows an inserted page")
    # Deleted text on that page: the word v2 replaced by 'deterministic'.
    markup(2, "Squiggly", around(2, 2, 10), GREEN, False,
           "the middle word was replaced by 'deterministic' in v2")
    # A table cell v2 changed.
    markup(2, "Highlight", "LayoutAnalyzer", YELLOW, True,
           "the row label survives; only the cell beside it changed")
    # The footer date v2 changed on every page.
    markup(3, "Highlight", "2026-08-31", ORANGE, False, "every footer's date changed in v2")
    # Survives on page 4 -> v2 page 5.
    markup(3, "Underline", first_words(3, 2, 4), BLUE, True,
           "text unchanged, its page now follows an inserted page")

    figure = [r for r in lines_v1[1] if r["kind"] == "image"][0]
    note_beside(1, (r3(figure["x"] + figure["width"] + 6), r3(figure["y"]),
                    r3(figure["x"] + figure["width"] + 26), r3(figure["y"] + 20)),
                "A note beside the figure.",
                "placed relative to the nearest text line, which v2 keeps")
    table = [r for r in lines_v1[2] if r["kind"] == "cell" and r.get("row") == 0 and r.get("col") == 0][0]
    note_beside(2, (r3(table["x"] - 30), r3(table["y"] - 2), r3(table["x"] - 10), r3(table["y"] + 18)),
                "A note beside the table.",
                "placed relative to the nearest text line on a page that moved")

    def image(pdf):
        return [(b"/XObject", pb.dictionary([(b"/Im1", pb.ref(_COMPARE._image(pdf)))]))]  # noqa
    data = f.build(resources_extra=image)
    return data, f.label(revised="compare/report-v2.pdf"), migration


# ---------------------------------------------------------------------------------------------

EXCHANGE = ("every-type", "threads")
MIGRATION_LABEL = "labels/annots-migration.json"


def label_name(fixture):
    stem = fixture.replace(".pdf", "")
    return "labels/%s.json" % (stem if stem.startswith("annots-") else "annots-" + stem)


def generate():
    out = {}
    for name, builder in FILES.items():
        result = builder()
        out["ordinary/" + name] = result[0]
        out[label_name(name)] = result[1]
        if name == "annots-report-v1.pdf":
            migration = result[2]
            out[MIGRATION_LABEL] = (json.dumps({
                "file": "ordinary/annots-report-v1.pdf", "revised": "compare/report-v2.pdf",
                "generator": GENERATOR,
                "pageMap": {"0": 0, "1": 1, "2": 3, "3": 4},
                "measures": "guide 3.5: precision 100 %, recall >= 95 % of placeable, every "
                            "unplaceable reported and none placed; markup quads within 1 pt, "
                            "other annotations within 2 pt",
                "annotations": migration}, ensure_ascii=False, indent=1) + "\n").encode("utf-8")
    fdf, xfdf = every_type_exchange()
    out["exchange/every-type.fdf"] = fdf
    out["exchange/every-type.xfdf"] = xfdf
    fdf, xfdf = threads_exchange()
    out["exchange/threads.fdf"] = fdf
    out["exchange/threads.xfdf"] = xfdf
    good = xfdf
    # The two hostile XFDF files are derived from a valid one, so each differs from it in exactly
    # the damage it is named for.
    cut = good.index(b"</annots>")
    out["exchange/malformed.xfdf"] = good[:cut - 40]
    out["exchange/external-entity.xfdf"] = good.replace(
        b'<?xml version="1.0" encoding="UTF-8"?>\n',
        b'<?xml version="1.0" encoding="UTF-8"?>\n'
        b'<!DOCTYPE xfdf [ <!ENTITY rawpageprobe SYSTEM "file:///C:/Windows/win.ini"> ]>\n', 1
    ).replace(b"<contents>Is this figure current?</contents>",
              b"<contents>&rawpageprobe;</contents>", 1)
    out["stamps/seal-rgba.png"] = stamp_seal_png()
    out["stamps/signature.jpg"] = stamp_signature_jpeg()
    return out


if __name__ == "__main__":
    for relative, data in generate().items():
        target = ROOT / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        print("wrote %-38s %9d bytes" % (relative, len(data)))
