#!/usr/bin/env python3
"""Generates `corpus/compare/` -- before/after pairs whose every difference is written down.

WHY. Stage 5's `core/text/DocumentComparer` (plan D5-17) is graded on pairs whose changes are KNOWN:
`Stage5-TextSearch-Testing-Guide.md` section 3.4 asks that every labelled change be found with its
kind, that an identical pair report nothing, and that two unrelated documents finish inside a bound.
So each pair here is built from a small document MODEL, revised by a direct edit of that model, and
accompanied by `<name>.expected.json` listing the changes as records a program can apply.

THE CHECK THAT KEEPS THE LABELS HONEST (`--self-test`, guide case A-15). A change list is a claim, and
a claim written by the same hand that made the edit is easy to get wrong in the direction nobody
notices -- an edit the list forgets. So the self-test re-derives every revised document from its base
by APPLYING the recorded changes with code of its own (`apply()` below shares nothing with the
functions that made the edits), and requires the result to equal the revised model exactly. An edit
the list does not mention, or a record that does not describe its edit, fails. Beside that it checks
what each kind of pair promises about TEXT: a text edit moves exactly the words its records name, an
image move or a whitespace change moves none, the identical pair has no records, and the unrelated
pair shares no word at all. Its controls damage a pair four ways and require each to be caught.

    python corpus/make-compare.py               # write compare/
    python corpus/make-compare.py --self-test   # the check above, and its controls

WHAT THIS DOES NOT CLAIM. These are documents we generated, in base-14 Helvetica, with edits of the
kinds listed and nothing else. A comparison that is right on them is right on them -- the real
revisions `Features.md` section 1.5 is about are not in the corpus, by the user's decision
(`Stage5-TextSearch-Plan.md` D5-23).
"""

import copy
import json
import pathlib
import sys
from collections import Counter

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import pdfbuild as pb                                                 # noqa: E402

HELVETICA = [
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556,
    556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667,
    611, 778, 722, 278, 500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667,
    667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500,
    222, 833, 556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584]


def width(text, size):
    """Helvetica advance widths -- the same table `make-text.py` records, and why it is exact."""
    return sum(HELVETICA[ord(c) - 32] for c in text) * size / 1000.0


# ---------------------------------------------------------------------------------------------
# The model
# ---------------------------------------------------------------------------------------------
# A document is {"header", "footerDate", "numberOffset", "columns", "pages"}; a page is a list of
# items; an item is a paragraph {"type": "para", "words", "double"} (`double` lists the word
# indexes followed by TWO spaces -- the whitespace-only edit), an image {"type": "image", "dx"},
# a ruled table {"type": "table", "rows"}, or a title {"type": "title", "text"}. `numberOffset`
# None means the pages carry no number.

def para(seed, count, vocabulary=None):
    words = pb.words_for(seed, count) if vocabulary is None else _walk(vocabulary, seed, count)
    words[0] = words[0].capitalize()
    words[-1] += "."
    return {"type": "para", "words": words, "double": []}


def _walk(vocabulary, seed, count):
    out, state = [], seed & 0xFFFFFFFF
    for _ in range(count):
        state = (1664525 * state + 1013904223) % (2 ** 32)
        out.append(vocabulary[state % len(vocabulary)])
    return out


def base_document():
    """Three pages, one column, a running header, a dated footer with page numbers, an image."""
    return {
        "header": "Quarterly engine notes", "footerDate": "2026-08-31", "numberOffset": 0,
        "columns": 1,
        "pages": [
            [para(7001, 58), para(7002, 64), para(7003, 49), para(7004, 61)],
            [para(7011, 55), {"type": "image", "dx": 0}, para(7012, 60), para(7013, 52)],
            [para(7021, 66), para(7022, 50), para(7023, 57)],
        ],
    }


# ---------------------------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------------------------

SIZE = 10.0
LEADING = 13.0
GAP = 8.0
IMAGE_W, IMAGE_H = 160.0, 90.0


def para_lines(item, frame_width):
    """Greedy wrap by measured width. Double spaces are real spaces and are measured."""
    lines, current, current_text = [], [], ""
    for index, word in enumerate(item["words"]):
        separator = "" if not current else ("  " if (index - 1) in item["double"] else " ")
        trial = current_text + separator + word
        if current and width(trial, SIZE) > frame_width:
            lines.append(current_text)
            current, current_text = [word], word
        else:
            current.append(word)
            current_text = trial
    if current:
        lines.append(current_text)
    return lines


def render_page(doc, index, image_ref):
    ops = []
    page = doc["pages"][index]
    if doc["header"]:
        ops.append(pb.show_text(60, 812, 8, doc["header"]))
    footer = []
    if doc["footerDate"]:
        footer.append(doc["footerDate"])
    if doc["numberOffset"] is not None:
        footer.append("page %d" % (index + 1 + doc["numberOffset"]))
    if footer:
        ops.append(pb.show_text(60, 30, 8, " - ".join(footer)))

    if doc["columns"] == 1:
        frames = [(60.0, 475.0)]
    else:
        frames = [(54.0, 230.0), (311.0, 230.0)]
    top = 790.0
    items = list(page)
    if items and items[0]["type"] == "title":
        ops.append(b"BT /F2 18 Tf 1 0 0 1 54 %s Tm %s Tj ET" % (pb.num(top - 10),
                                                             pb.literal(items[0]["text"])))
        top -= 44
        items = items[1:]
    frame = 0
    y = top
    for item in items:
        x, frame_width = frames[frame]
        if item["type"] == "para":
            lines = para_lines(item, frame_width)
            for line in lines:
                if y < 60:
                    frame += 1
                    assert frame < len(frames), "a comparison page overflowed its frames"
                    x, frame_width = frames[frame]
                    y = top
                ops.append(pb.show_text(x, y, SIZE, line))
                y -= LEADING
            y -= GAP
        elif item["type"] == "image":
            if y - IMAGE_H < 60:
                frame += 1
                assert frame < len(frames), "a comparison page overflowed its frames"
                x, frame_width = frames[frame]
                y = top
            ops.append(b"q %s 0 0 %s %s %s cm %s Do Q" % (
                pb.num(IMAGE_W), pb.num(IMAGE_H), pb.num(x + item["dx"]),
                pb.num(y - IMAGE_H + SIZE), image_ref))
            y -= IMAGE_H + GAP
        elif item["type"] == "table":
            rows = item["rows"]
            height = 16.0 * len(rows)
            if y - height < 60:
                frame += 1
                assert frame < len(frames), "a comparison page overflowed its frames"
                x, frame_width = frames[frame]
                y = top
            cols = len(rows[0])
            # the first column holds names and takes half the width; the rest share the other half
            edges = [x, x + frame_width / 2]
            for col in range(1, cols):
                edges.append(edges[-1] + frame_width / 2 / (cols - 1))
            table_top = y + SIZE
            rules = [b"0 G 0.5 w"]
            for row in range(len(rows) + 1):
                ry = table_top - row * 16.0
                rules.append(b"%s %s m %s %s l S" % (pb.num(x), pb.num(ry), pb.num(x + frame_width),
                                                     pb.num(ry)))
            for rx in edges:
                rules.append(b"%s %s m %s %s l S" % (pb.num(rx), pb.num(table_top), pb.num(rx),
                                                     pb.num(table_top - height)))
            ops.append(b" ".join(rules))
            for row, values in enumerate(rows):
                for col, value in enumerate(values):
                    ops.append(pb.show_text(edges[col] + 3, table_top - (row + 1) * 16.0 + 4.5,
                                            8.5, value))
            y -= height + GAP
    return b"\n".join(ops) + b"\n"


def _image(pdf):
    """One shared grey image -- concentric rings, 48 x 27 -- so an image MOVE is a move of
    something with structure, which a raster comparison can locate."""
    width_px, height_px = 48, 27
    data = bytearray()
    for row in range(height_px):
        for col in range(width_px):
            distance = ((col - 24) ** 2 + ((row - 13) * 1.7) ** 2) ** 0.5
            data.append(40 + int(distance * 11) % 180)
    encoded, filter_name = pb.flate(bytes(data))
    return pdf.add_stream([(b"/Type", b"/XObject"), (b"/Subtype", b"/Image"),
                           (b"/Width", pb.num(width_px)), (b"/Height", pb.num(height_px)),
                           (b"/ColorSpace", b"/DeviceGray"), (b"/BitsPerComponent", b"8"),
                           (b"/Filter", filter_name)], encoded)


def build_pdf(doc, title, author, date):
    pdf = pb.Pdf()
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    image = _image(pdf)
    resources = pb.dictionary([
        (b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)), (b"/F2", pb.ref(bold))])),
        (b"/XObject", pb.dictionary([(b"/Im1", pb.ref(image))]))])
    pages = []
    for index in range(len(doc["pages"])):
        contents = pb.content_stream(pdf, render_page(doc, index, b"/Im1"))
        pages.append(lambda parent, contents=contents: pb.page(parent, pb.A4, contents, resources))
    catalog, _, _ = pb.simple_document(pdf, pages)
    info = pdf.add(pb.dictionary([
        (b"/Title", pb.utf16(title)), (b"/Author", pb.utf16(author)),
        (b"/Subject", pb.utf16("Stage 5 comparison fixture")),
        (b"/Producer", pb.literal(pb.PRODUCER)),
        (b"/CreationDate", pb.literal(date)), (b"/ModDate", pb.literal(date))]))
    return pdf.serialize(catalog, info)


# ---------------------------------------------------------------------------------------------
# apply() -- the INDEPENDENT half: re-derives a revised model from a base and a change list
# ---------------------------------------------------------------------------------------------

def apply(base, changes):
    doc = copy.deepcopy(base)
    for change in changes:
        kind = change["kind"]
        if kind == "insert":
            doc["pages"][change["page"]][change["item"]]["words"][change["at"]:change["at"]] = \
                list(change["after"])
        elif kind == "delete":
            words = doc["pages"][change["page"]][change["item"]]["words"]
            assert words[change["at"]:change["at"] + len(change["before"])] == change["before"]
            del words[change["at"]:change["at"] + len(change["before"])]
        elif kind == "replace":
            words = doc["pages"][change["page"]][change["item"]]["words"]
            assert words[change["at"]:change["at"] + len(change["before"])] == change["before"]
            words[change["at"]:change["at"] + len(change["before"])] = list(change["after"])
        elif kind == "move":
            page = doc["pages"][change["page"]]
            item = page.pop(change["from"])
            assert item["words"] == change["words"]
            page.insert(change["to"], item)
        elif kind == "image-move":
            item = doc["pages"][change["page"]][change["item"]]
            assert item["type"] == "image" and item["dx"] == change["before"]
            item["dx"] = change["after"]
        elif kind == "table-cell":
            rows = doc["pages"][change["page"]][change["item"]]["rows"]
            assert rows[change["row"]][change["col"]] == change["before"]
            rows[change["row"]][change["col"]] = change["after"]
        elif kind == "whitespace":
            doc["pages"][change["page"]][change["item"]]["double"] = list(change["doubleAfter"])
        elif kind == "page-insert":
            doc["pages"].insert(change["at"], copy.deepcopy(change["items"]))
        elif kind == "page-delete":
            assert doc["pages"][change["at"]] == change["items"]
            del doc["pages"][change["at"]]
        elif kind == "page-order":
            doc["pages"] = [doc["pages"][index] for index in change["order"]]
        elif kind == "header":
            assert doc["header"] == change["before"]
            doc["header"] = change["after"]
        elif kind == "footer-date":
            assert doc["footerDate"] == change["before"]
            doc["footerDate"] = change["after"]
        elif kind == "page-numbers":
            assert doc["numberOffset"] == change["before"]
            doc["numberOffset"] = change["after"]
        else:
            raise ValueError("unknown change kind %r" % kind)
    return doc


def tokens(doc):
    """Every word a reader sees, in page order: header, items, footer."""
    out = []
    for index, page in enumerate(doc["pages"]):
        if doc["header"]:
            out += doc["header"].split()
        for item in page:
            if item["type"] == "para":
                out += item["words"]
            elif item["type"] == "table":
                for row in item["rows"]:
                    for value in row:
                        out += value.split()
            elif item["type"] == "title":
                out += item["text"].split()
        if doc["footerDate"]:
            out.append(doc["footerDate"])
        if doc["numberOffset"] is not None:
            out += ["page", str(index + 1 + doc["numberOffset"])]
    return out


TEXT_KINDS = {"insert", "delete", "replace", "table-cell", "header", "footer-date"}


def check_pair(name, base, revised, record):
    """The self-test for one pair. Returns a list of problems."""
    problems = []
    changes = record["changes"]
    if record["pair"] == "unrelated":
        # No change list can relate two documents that share nothing, so the claim here is the
        # vocabulary one alone.
        shared = set(tokens(base)) & set(tokens(revised))
        if shared:
            problems.append("PAIR      %s -- the unrelated pair shares %d words, e.g. %r"
                            % (name, len(shared), sorted(shared)[:3]))
        return problems
    try:
        derived = apply(base, changes)
    except (AssertionError, IndexError, KeyError, ValueError) as error:
        return ["PAIR      %s -- a change does not apply to its base: %r" % (name, error)]
    if derived != revised:
        problems.append("PAIR      %s -- the change list does not produce the revised document"
                        % name)
    kinds = {change["kind"] for change in changes}
    if record["pair"] == "identical":
        if changes or base != revised:
            problems.append("PAIR      %s -- the identical pair is not identical" % name)
    elif kinds and kinds <= TEXT_KINDS:
        removed = Counter(tokens(base)) - Counter(tokens(revised))
        added = Counter(tokens(revised)) - Counter(tokens(base))
        claimed_removed, claimed_added = Counter(), Counter()
        pages = len(base["pages"])
        for change in changes:
            repeat = pages if change["kind"] in ("header", "footer-date") else 1
            before = change.get("before", [])
            after = change.get("after", [])
            before = before.split() if isinstance(before, str) else before
            after = after.split() if isinstance(after, str) else after
            for _ in range(repeat):
                claimed_removed.update(before)
                claimed_added.update(after)
        net_removed = claimed_removed - claimed_added
        net_added = claimed_added - claimed_removed
        if removed != net_removed or added != net_added:
            problems.append("PAIR      %s -- the words that changed are not the words claimed"
                            % name)
    elif kinds and kinds <= {"image-move", "whitespace", "move"}:
        if Counter(tokens(base)) != Counter(tokens(revised)):
            problems.append("PAIR      %s -- a %s changed words" % (name, "/".join(sorted(kinds))))
    return problems


# ---------------------------------------------------------------------------------------------
# The pairs -- each revised model made by a DIRECT edit, never by apply()
# ---------------------------------------------------------------------------------------------

VOCABULARY_A = [
    "harbour", "lantern", "meadow", "orchard", "pebble", "quarry", "saddle", "tundra", "valley",
    "willow", "canyon", "delta", "estuary", "fjord", "glacier", "heath", "island", "jetty", "knoll",
    "lagoon", "marsh", "oasis", "prairie", "reef", "savanna", "thicket", "upland", "vineyard",
    "wetland", "cliff", "dune", "grove", "hollow", "inlet", "moor", "plateau", "ridge", "spring",
    "summit", "brook"]
VOCABULARY_B = [
    "algebra", "bracket", "cipher", "divisor", "exponent", "fraction", "gradient", "hyperbola",
    "integer", "juxtapose", "kernel", "lemma", "matrix", "numeral", "operand", "parabola",
    "quotient", "radix", "scalar", "tangent", "unary", "vector", "axiom", "binomial", "calculus",
    "determinant", "eigenvalue", "factorial", "geometry", "helix", "identity", "logarithm",
    "modulus", "norm", "ordinal", "polynomial", "rational", "sequence", "theorem", "variance"]

DATE = b"D:20260920100000Z"


def pairs():
    """[(name, base model, revised model, record)] -- the record is `expected.json`'s body."""
    out = []
    base = base_document()

    def pair(name, revised, changes, summary, kind="edit"):
        out.append((name, base, revised, {"pair": kind, "changes": changes, "summary": summary}))

    revised = copy.deepcopy(base)
    revised["pages"][0][1]["words"][9:9] = ["urgently"]
    pair("word-inserted", revised,
         [{"kind": "insert", "page": 0, "item": 1, "at": 9, "after": ["urgently"]}],
         ["page 1, paragraph 2: the word 'urgently' inserted after its ninth word"])

    revised = copy.deepcopy(base)
    gone = revised["pages"][1][2]["words"][14]
    del revised["pages"][1][2]["words"][14]
    pair("word-deleted", revised,
         [{"kind": "delete", "page": 1, "item": 2, "at": 14, "before": [gone]}],
         ["page 2, paragraph 2: the word '%s' deleted" % gone])

    revised = copy.deepcopy(base)
    old = revised["pages"][2][0]["words"][20]
    revised["pages"][2][0]["words"][20] = "carefully"
    pair("word-replaced", revised,
         [{"kind": "replace", "page": 2, "item": 0, "at": 20, "before": [old],
           "after": ["carefully"]}],
         ["page 3, paragraph 1: '%s' replaced by 'carefully'" % old])

    revised = copy.deepcopy(base)
    moved = revised["pages"][0].pop(3)
    revised["pages"][0].insert(0, moved)
    pair("paragraph-moved", revised,
         [{"kind": "move", "page": 0, "from": 3, "to": 0, "words": moved["words"]}],
         ["page 1: the fourth paragraph (%d words) moved to the top of the page"
          % len(moved["words"])])

    revised = copy.deepcopy(base)
    revised["pages"][1][1]["dx"] = 180
    pair("image-moved", revised,
         [{"kind": "image-move", "page": 1, "item": 1, "before": 0, "after": 180}],
         ["page 2: the image moved 180 pt to the right; no text changed"])

    revised = copy.deepcopy(base)
    inserted = [para(7031, 62), para(7032, 55)]
    revised["pages"].insert(1, inserted)
    pair("page-inserted", revised,
         [{"kind": "page-insert", "at": 1, "items": inserted}],
         ["a new page inserted as page 2; the old pages 2 and 3 are now 3 and 4, and their "
          "footer numbers moved with them"])

    revised = copy.deepcopy(base)
    removed_page = revised["pages"].pop(1)
    pair("page-deleted", revised,
         [{"kind": "page-delete", "at": 1, "items": removed_page}],
         ["page 2 deleted; the old page 3 is now page 2, and its footer number moved with it"])

    revised = copy.deepcopy(base)
    revised["pages"] = [revised["pages"][0], revised["pages"][2], revised["pages"][1]]
    pair("pages-reordered", revised,
         [{"kind": "page-order", "order": [0, 2, 1]}],
         ["pages 2 and 3 swapped; the footer numbers stay 1, 2, 3 in page order"])

    revised = copy.deepcopy(base)
    revised["header"] = "Quarterly engine notes revised"
    pair("header-changed", revised,
         [{"kind": "header", "before": "Quarterly engine notes",
           "after": "Quarterly engine notes revised"}],
         ["the running header gains the word 'revised' on every page"])

    revised = copy.deepcopy(base)
    revised["numberOffset"] = 4
    pair("page-numbers-shifted", revised,
         [{"kind": "page-numbers", "before": 0, "after": 4}],
         ["every footer's page number is 4 higher; nothing else changed"])

    revised = copy.deepcopy(base)
    revised["pages"][0][0]["double"] = [3, 11, 17]
    pair("whitespace-only", revised,
         [{"kind": "whitespace", "page": 0, "item": 0, "doubleAfter": [3, 11, 17]}],
         ["page 1, paragraph 1: three single spaces became double spaces; no word changed"])

    pair("identical", copy.deepcopy(base), [], ["nothing: the pages are identical and only the "
                                                "Info /Title differs"], kind="identical")

    unrelated_a = {"header": None, "footerDate": None, "numberOffset": None, "columns": 1,
                   "pages": [[para(8000 + i * 4 + j, 52, VOCABULARY_A) for j in range(4)]
                             for i in range(50)]}
    unrelated_b = {"header": None, "footerDate": None, "numberOffset": None, "columns": 1,
                   "pages": [[para(9000 + i * 4 + j, 52, VOCABULARY_B) for j in range(4)]
                             for i in range(50)]}
    out.append(("unrelated", unrelated_a, unrelated_b,
                {"pair": "unrelated", "changes": None,
                 "summary": ["two 50-page documents that share no word: the bound on a "
                             "comparison of documents with nothing in common"]}))

    report_v1, report_v2, changes, summary = report()
    out.append(("report", report_v1, report_v2,
                {"pair": "report", "changes": changes, "summary": summary}))
    return out


def report():
    """The walkable pair (plan D5-23): a four-page two-column report revised the way a person
    revises one -- several edits at once, a page inserted, the date changed everywhere."""
    table = [["Component", "Cases", "State"], ["PageTextModel", "12", "planned"],
             ["LayoutAnalyzer", "18", "planned"], ["SearchEngine", "24", "planned"],
             ["DocumentComparer", "15", "planned"]]
    v1 = {"header": None, "footerDate": "2026-08-31", "numberOffset": 0, "columns": 2,
          "pages": [
              [{"type": "title", "text": "Engine report"}]
              + [para(7101 + i, 44 + (i * 7) % 16) for i in range(8)],
              [para(7111, 55), {"type": "image", "dx": 0}]
              + [para(7112 + i, 46 + (i * 5) % 14) for i in range(7)],
              [{"type": "table", "rows": table}]
              + [para(7121 + i, 45 + (i * 3) % 15) for i in range(8)],
              [para(7131 + i, 47 + (i * 9) % 13) for i in range(7)],
          ]}
    v2 = copy.deepcopy(v1)
    changes, summary = [], []

    old = v2["pages"][0][2]["words"][6]
    v2["pages"][0][2]["words"][6] = "throughput"
    changes.append({"kind": "replace", "page": 0, "item": 2, "at": 6, "before": [old],
                    "after": ["throughput"]})
    summary.append("page 1: '%s' replaced by 'throughput'" % old)

    moved = v2["pages"][0].pop(8)
    v2["pages"][0].insert(3, moved)
    changes.append({"kind": "move", "page": 0, "from": 8, "to": 3, "words": moved["words"]})
    summary.append("page 1: the last paragraph moved up to become the third")

    sentence = "The second window now queues its viewport at prefetch.".split()
    at = len(v2["pages"][1][0]["words"])
    v2["pages"][1][0]["words"] += sentence
    changes.append({"kind": "insert", "page": 1, "item": 0, "at": at, "after": sentence})
    summary.append("page 2: a sentence added to the end of the first paragraph")

    v2["pages"][1][1]["dx"] = 40
    changes.append({"kind": "image-move", "page": 1, "item": 1, "before": 0, "after": 40})
    summary.append("page 2: the figure moved 40 pt to the right")

    v2["pages"][2][0]["rows"][2][1] = "21"
    changes.append({"kind": "table-cell", "page": 2, "item": 0, "row": 2, "col": 1,
                    "before": "18", "after": "21"})
    summary.append("page 3: the table's LayoutAnalyzer row now says 21 cases, not 18")

    old = v2["pages"][2][2]["words"][10]
    v2["pages"][2][2]["words"][10] = "deterministic"
    changes.append({"kind": "replace", "page": 2, "item": 2, "at": 10, "before": [old],
                    "after": ["deterministic"]})
    summary.append("page 3: '%s' replaced by 'deterministic'" % old)

    new_page = [para(7141 + i, 46 + (i * 4) % 15) for i in range(8)]
    v2["pages"].insert(2, new_page)
    changes.append({"kind": "page-insert", "at": 2, "items": new_page})
    summary.append("a new page inserted as page 3; the old pages 3 and 4 are now 4 and 5, and "
                   "their footer numbers moved with them")

    v2["footerDate"] = "2026-09-29"
    changes.append({"kind": "footer-date", "before": "2026-08-31", "after": "2026-09-29"})
    summary.append("every footer's date changed from 2026-08-31 to 2026-09-29")
    return v1, v2, changes, summary


# ---------------------------------------------------------------------------------------------

def generate():
    out = {}
    base_written = False
    for name, base, revised, record in pairs():
        if name == "unrelated":
            base_name, revised_name = "compare/unrelated-a.pdf", "compare/unrelated-b.pdf"
            out[base_name] = build_pdf(base, "Unrelated A", "Kofi Mensah", DATE)
        elif name == "report":
            base_name, revised_name = "compare/report-v1.pdf", "compare/report-v2.pdf"
            out[base_name] = build_pdf(base, "Engine report", "Kofi Mensah", DATE)
        else:
            base_name, revised_name = "compare/base.pdf", "compare/%s.pdf" % name
            if not base_written:
                out[base_name] = build_pdf(base, "Comparison base", "Kofi Mensah", DATE)
                base_written = True
        title = {"unrelated": "Unrelated B", "report": "Engine report, revised",
                 "identical": "Comparison base, again"}.get(name, "Comparison: " + name)
        out[revised_name] = build_pdf(revised, title, "Kofi Mensah", DATE)
        body = {"base": base_name, "revised": revised_name, "generator": "corpus/make-compare.py",
                **record}
        out["compare/%s.expected.json" % name] = (
            json.dumps(body, ensure_ascii=False, indent=1) + "\n").encode("utf-8")
    return out


def self_test(verbose=True):
    """Every pair checked, then four controls that must each be caught. Returns problems."""
    problems = []
    all_pairs = pairs()
    for name, base, revised, record in all_pairs:
        if record["changes"] is None:
            record = dict(record, changes=[])
        problems += check_pair(name, base, revised, record)

    controls = []
    by_name = {name: (base, revised, record) for name, base, revised, record in all_pairs}

    base, revised, record = by_name["report"]
    forgotten = dict(record, changes=record["changes"][1:])
    controls.append(("a forgotten change is caught", check_pair("control", base, revised, forgotten)))

    base, revised, record = by_name["word-replaced"]
    extra = copy.deepcopy(revised)
    extra["pages"][2][1]["words"][3] = "unclaimed"
    controls.append(("an unclaimed edit is caught", check_pair("control", base, extra, record)))

    base, revised, record = by_name["identical"]
    touched = copy.deepcopy(revised)
    touched["pages"][0][0]["words"][0] = "Touched"
    controls.append(("a touched identical pair is caught",
                     check_pair("control", base, touched, record)))

    base, revised, record = by_name["unrelated"]
    overlapping = copy.deepcopy(revised)
    overlapping["pages"][49][3]["words"][5] = VOCABULARY_A[0]
    controls.append(("an unrelated pair sharing a word is caught",
                     check_pair("control", base, overlapping, record)))

    for label, found in controls:
        if verbose:
            print("  %-36s %s" % (label, "detected" if found else "NOT DETECTED"))
        if not found:
            problems.append("CONTROL   %s -- the compare self-test did not fire" % label)
    if verbose and not problems:
        print("compare self-test ok: %d pairs checked, %d of %d controls fired"
              % (len(all_pairs), len(controls), len(controls)))
    return problems


if __name__ == "__main__":
    if "--self-test" in sys.argv[1:]:
        found = self_test()
        for problem in found:
            print(problem)
        sys.exit(1 if found else 0)
    root = pathlib.Path(__file__).resolve().parent
    for relative, data in generate().items():
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        print("wrote %-38s %8d bytes" % (relative, len(data)))
