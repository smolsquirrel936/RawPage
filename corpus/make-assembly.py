#!/usr/bin/env python3
"""Generates Stage 7's assembly corpus -- the fixtures in `assembly/` and their GROUND TRUTH in
`labels/assembly-*.json` (`Stage7-Assembly-Plan.md` section 5.2, decision D7-17).

WHY EVERY PAGE PRINTS ITS OWN NAME. Stage 7 moves, deletes, copies and merges pages, and the
question every case asks afterwards is *which page is this?* The obvious oracle -- our own page
map -- agrees with itself when it is wrong (plan D7-18). So every page a reference case uses draws
its IDENTITY, `P-0007`, in 48-point Helvetica-Bold across its centre, and a reference is graded by
the identity PDFium extracts from the page it lands on. An identity is a tag of capitals and digits,
a hyphen and four digits (`IDENTITY_PATTERN`), and `caption()` refuses any other text on a page that
would match it -- so the one token on a page that looks like a name IS its name. The tags are
distinct across the fixtures (`MA-`, `MB-`, `MC-` for the three merge sources), so a page carried
from one file into another still says where it came from. `refs-none.pdf` repeats
`refs-every-kind.pdf`'s twelve on purpose: it is the same pages with nothing pointing at them, and
`tagged-headings.pdf` and `untagged-headings.pdf` share `TG-` because they are the same text.

WHAT A LABEL HOLDS (`labels/assembly-<name>.json`). Per page: its index, the object number it was
written at, its identity (`null` for a page that shows none -- `split-blanks.pdf`'s blank pages) and
the boxes and rotation it ends up with. Then whatever the fixture is for, each reference given as
the object that carries it, the form it is written in, and the IDENTITY of the page it targets --
never only an index, so the grade can be read off the target page itself. `fixturecheck.py`'s
`check_assembly` holds every claim to the bytes; `render.assembly labels ...` holds the identities
and the outline and named-destination targets to what PDFium reads (guide case A-15).

DETERMINISM. No clock, no `random`, no `math.sin`/`math.cos`. The watermark another producer drew
is rotated by 45 degrees through `math.sqrt(0.5)`, which IEEE 754 rounds exactly.

WHAT THIS DOES NOT CLAIM. These are documents we generated, in base-14 faces, with references in
the shapes ISO 32000-1 describes; `decorated-elsewhere.pdf` imitates the SHAPE Acrobat writes a
header and a watermark in, and was not written by Acrobat (D2-11: every byte is ours). A reference
repaired correctly here is repaired correctly in these files; what other readers make of ours is
the guide's section 6.

    python corpus/make-assembly.py           # write every file this generator owns
    python corpus/make-corpus.py --verify    # prove they regenerate byte-identically
"""

import importlib.util
import json
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

import pdfbuild as pb                                                 # noqa: E402


def _load(filename):
    spec = importlib.util.spec_from_file_location(
        "_assembly_" + filename.replace("-", "_").replace(".py", ""), ROOT / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# The Helvetica widths are `make-text.py`'s, so an identity is centred by the same table a label's
# extents come from everywhere else in the corpus.
_TEXT = _load("make-text.py")
width = _TEXT.width
wrap = _TEXT.wrap

GENERATOR = "corpus/make-assembly.py"

IDENTITY_PATTERN = r"[A-Z][A-Z0-9]*-[0-9]{4}"
_IDENTITY = re.compile(r"(?<![A-Za-z0-9-])" + IDENTITY_PATTERN + r"(?![A-Za-z0-9-])")
IDENTITY_SIZE = 48
# Where the identity of a heading fixture goes instead: 48-point text would be the largest on the
# page, and every heading generator Stage 7 builds (plan D7-10) would make it the first heading.
FOOTER_SIZE = 9

A4 = pb.A4
LETTER = pb.LETTER
A5 = pb.A5
A5_LANDSCAPE = (pb.A5[1], pb.A5[0])


def identity(tag, number):
    return "%s-%04d" % (tag, number)


def r3(value):
    return round(value, 3)


def box(x0, y0, x1, y1):
    return [r3(x0), r3(y0), r3(x1), r3(y1)]


def media_box(media):
    return box(0, 0, media[0], media[1])


# ---------------------------------------------------------------------------------------------
# Drawing
# ---------------------------------------------------------------------------------------------

def identity_ops(text, media, grey=0):
    """The identity across the page's centre, in Helvetica-Bold (`/F2`). `grey` 1 draws it white,
    which is `split-blanks.pdf`'s page that is blank to the eye and not to the engine."""
    x = (media[0] - width(text, IDENTITY_SIZE, bold=True)) / 2
    y = media[1] / 2 - IDENTITY_SIZE * 0.35
    return b"%s g %s 0 g" % (pb.num(grey), pb.show_text(x, y, IDENTITY_SIZE, text, b"/F2"))


def footer_identity_ops(text, media):
    x = (media[0] - width(text, FOOTER_SIZE)) / 2
    return pb.show_text(x, 30, FOOTER_SIZE, text, b"/F1")


def caption(x, y, size, text, font=b"/F1"):
    """Any text on a page other than its identity. It may not look like one (module docstring)."""
    if _IDENTITY.search(text):
        raise ValueError("caption %r would read as a page identity" % text)
    return pb.show_text(x, y, size, text, font)


def captions(media, lines, top=None, size=10):
    start = top if top is not None else media[1] - 56
    return [caption(48, start - index * (size + 4), size, line) for index, line in enumerate(lines)]


def stroke_rect(x0, y0, x1, y1, grey=0.4, line=0.75):
    return b"q %s G %s w %s %s %s %s re S Q" % (pb.num(grey), pb.num(line), pb.num(x0), pb.num(y0),
                                               pb.num(x1 - x0), pb.num(y1 - y0))


def fill_rect(x0, y0, x1, y1, grey):
    return b"q %s g %s %s %s %s re f Q" % (pb.num(grey), pb.num(x0), pb.num(y0), pb.num(x1 - x0),
                                          pb.num(y1 - y0))


def artifact(ops, subtype=None):
    """Content that belongs to no structure element, as a tagged document must mark it."""
    if subtype is None:
        return b"/Artifact BMC " + ops + b" EMC"
    return (b"/Artifact << /Type /Pagination /Subtype /%s >> BDC " % subtype.encode("ascii")
            + ops + b" EMC")


# ---------------------------------------------------------------------------------------------
# Objects every fixture uses
# ---------------------------------------------------------------------------------------------

class Doc:
    """One fixture under construction: its fonts, its page objects, and the label beside it."""

    def __init__(self, name, title):
        self.name = name
        self.title = title
        self.pdf = pb.Pdf()
        self.helv = pb.base14(self.pdf, "Helvetica")
        self.bold = pb.base14(self.pdf, "Helvetica-Bold")
        self.label_pages = []

    def fonts(self):
        return pb.dictionary([(b"/F1", pb.ref(self.helv)), (b"/F2", pb.ref(self.bold))])

    def resources(self, extra=()):
        return pb.dictionary([(b"/Font", self.fonts())] + list(extra))

    def contents(self, ops):
        return pb.content_stream(self.pdf, b"\n".join(ops) + b"\n")

    def note_page(self, index, number, ident, media, rotate=0, extra=None):
        entry = {"index": index, "object": number, "identity": ident, "mediaBox": media_box(media),
                 "rotate": rotate}
        if extra:
            entry.update(extra)
        self.label_pages.append(entry)

    def serialize(self, catalog, label):
        data = self.pdf.serialize(catalog, pb.info_object(self.pdf, self.title))
        body = {"file": "assembly/" + self.name, "generator": GENERATOR,
                "identity": {"pattern": IDENTITY_PATTERN, "font": "Helvetica-Bold",
                             "size": IDENTITY_SIZE, "placement": "centre"},
                "pages": self.label_pages}
        body.update(label)
        return data, (json.dumps(body, ensure_ascii=False, indent=1) + "\n").encode("utf-8")


def page_dict(parent, media, contents, resources, extra=()):
    pairs = [(b"/Type", b"/Page"), (b"/Parent", pb.ref(parent)),
             (b"/MediaBox", pb.rect(0, 0, media[0], media[1]))]
    if resources is not None:
        pairs.append((b"/Resources", resources))
    if contents is not None:
        pairs.append((b"/Contents", pb.ref(contents)))
    pairs.extend(extra)
    return pb.dictionary(pairs)


def pages_dict(kids, count, extra=()):
    return pb.dictionary([(b"/Type", b"/Pages"), (b"/Kids", pb.array([pb.ref(k) for k in kids])),
                          (b"/Count", pb.num(count))] + list(extra))


def dest(page_number, *view):
    return pb.array([pb.ref(page_number)] + [v if isinstance(v, bytes) else pb.num(v) for v in view])


def appearance(doc, w, h, ops, resources=None):
    pairs = [(b"/Type", b"/XObject"), (b"/Subtype", b"/Form"), (b"/BBox", pb.rect(0, 0, w, h))]
    if resources is not None:
        pairs.append((b"/Resources", resources))
    return doc.pdf.add_stream(pairs, ops)


def note_appearance(doc, colour=b"1 0.85 0.3"):
    return appearance(doc, 20, 20, b"q %s rg 0 0 0 RG 0.5 w 0.5 0.5 19 19 re B "
                                   b"0 G 4 14 m 16 14 l 4 10 m 16 10 l 4 6 m 12 6 l S Q" % colour)


def outline_items(doc, parent, items):
    """Writes a level of outline items. Each item is (title, target page object, view, form,
    children, open) with `form` "dest" or "action". Returns ([object numbers], label entries,
    visible count)."""
    numbers = [doc.pdf.reserve() for _ in items]
    labels = []
    visible = 0
    for position, (number, item) in enumerate(zip(numbers, items)):
        title, target, view, form, children, is_open, target_identity = item
        pairs = [(b"/Title", pb.utf16(title)), (b"/Parent", pb.ref(parent))]
        if position > 0:
            pairs.append((b"/Prev", pb.ref(numbers[position - 1])))
        if position + 1 < len(numbers):
            pairs.append((b"/Next", pb.ref(numbers[position + 1])))
        child_numbers, child_labels, child_visible = outline_items(doc, number, children)
        if child_numbers:
            pairs.append((b"/First", pb.ref(child_numbers[0])))
            pairs.append((b"/Last", pb.ref(child_numbers[-1])))
            pairs.append((b"/Count", pb.num(child_visible if is_open else -len(child_numbers))))
        if form == "dest":
            pairs.append((b"/Dest", view))
        else:
            pairs.append((b"/A", pb.dictionary([(b"/S", b"/GoTo"), (b"/D", view)])))
        doc.pdf.set(number, pb.dictionary(pairs))
        labels.append({"object": number, "title": title, "form": form, "targetPage": target,
                       "targetIdentity": target_identity, "open": is_open if children else None,
                       "children": child_labels})
        visible += 1 + (child_visible if is_open else 0)
    return numbers, labels, visible


def outline_root(doc, items):
    root = doc.pdf.reserve()
    numbers, labels, visible = outline_items(doc, root, items)
    doc.pdf.set(root, pb.dictionary([(b"/Type", b"/Outlines"), (b"/First", pb.ref(numbers[0])),
                                     (b"/Last", pb.ref(numbers[-1])), (b"/Count", pb.num(visible))]))
    return root, labels


def link(doc, page, rect, action_pairs):
    return doc.pdf.add(pb.dictionary(
        [(b"/Type", b"/Annot"), (b"/Subtype", b"/Link"), (b"/Rect", pb.rect(*rect)),
         (b"/P", pb.ref(page)), (b"/Border", pb.array([b"0", b"0", b"1"])),
         (b"/C", pb.array([b"0", b"0", b"1"]))] + action_pairs))


FILES = {}


def generator(name):
    def register(function):
        FILES[name] = function
        return function
    return register


# ---------------------------------------------------------------------------------------------
# refs-every-kind.pdf and its control refs-none.pdf
# ---------------------------------------------------------------------------------------------

REFS_PAGES = 12
# /PageLabels in three ranges: i ... iii, then 1 ... 6, then A-1 ... A-3.
REFS_LABELS = ["i", "ii", "iii", "1", "2", "3", "4", "5", "6", "A-1", "A-2", "A-3"]

# The links, page by page: (form, how the target is named, target index, caption).
REFS_LINKS = [
    (0, "dest", 8, "link 1: /Dest names the target page by reference"),
    (0, "goto", 5, "link 2: a GoTo action names the target page by reference"),
    (1, "destString", 6, "link 3: /Dest is a name-tree destination (a string)"),
    (1, "destName", 3, "link 4: /Dest is a /Dests dictionary destination (a name)"),
    (2, "gotoNamed", 9, "link 5: a GoTo action names a name-tree destination"),
    (3, "goToR", 4, "link 6: a GoToR action into this same file, by page number"),
]
# The named destinations: (name, form, entry form, target index, view).
REFS_NAMED = [
    ("dest-alpha", "namesTree", "array", 2, (b"/Fit",)),
    ("dest-beta", "namesTree", "dictionary", 6, (b"/XYZ", 72, 700, b"null")),
    ("dest-gamma", "namesTree", "array", 9, (b"/FitH", 600)),
    ("old-delta", "destsDict", "array", 3, (b"/Fit",)),
    ("old-epsilon", "destsDict", "array", 10, (b"/XYZ", b"null", b"null", 0)),
]
# Two article threads, each bead on the page named, in reading order.
REFS_THREADS = [("Thread one", [4, 5, 6]), ("Thread two", [9, 11])]
REFS_OPEN_ACTION = 2


def _refs_page_ops(doc, page_numbers, tag, with_refs):
    """The twelve identity pages both fixtures share: {page object: content operators}."""
    page_ops = {}
    for index, number in enumerate(page_numbers):
        ident = identity(tag, index + 1)
        if with_refs:
            lines = ["refs-every-kind.pdf - page %d of %d, labelled %s"
                     % (index + 1, REFS_PAGES, REFS_LABELS[index])]
            lines += [text for page, _, _, text in REFS_LINKS if page == index]
        else:
            lines = ["refs-none.pdf - page %d of %d; nothing in this file points at any page"
                     % (index + 1, REFS_PAGES)]
        ops = [artifact(b"\n".join(captions(A4, lines)))] if with_refs else captions(A4, lines)
        if with_refs:
            # A link's rectangle, drawn so a person can see where to click.
            on_page = [entry for entry in REFS_LINKS if entry[0] == index]
            for position in range(len(on_page)):
                y = A4[1] - 56 - (position + 1) * 14
                ops.append(artifact(stroke_rect(44, y - 4, 330, y + 11, grey=0.2)))
            ops.append(b"/P << /MCID 0 >> BDC " + identity_ops(ident, A4) + b" EMC")
        else:
            ops.append(identity_ops(ident, A4))
        doc.note_page(index, number, ident, A4, extra={"pageLabel": REFS_LABELS[index]}
                      if with_refs else None)
        page_ops[number] = ops
    return page_ops


def _refs_document(name, title, with_refs):
    doc = Doc(name, title)
    pdf = doc.pdf
    pages_number = pdf.reserve()
    page_numbers = [pdf.reserve() for _ in range(REFS_PAGES)]
    page_ops = _refs_page_ops(doc, page_numbers, "P", with_refs)
    ident = [identity("P", i + 1) for i in range(REFS_PAGES)]
    label = {}
    page_extra = {number: [] for number in page_numbers}

    catalog_extra = []
    if with_refs:
        # Named destinations, both forms.
        tree_entries = []
        named_labels = []
        dests_dict = []
        for dname, form, entry_form, target, view in REFS_NAMED:
            array = dest(page_numbers[target], *view)
            if form == "namesTree":
                tree_entries.append((dname, array if entry_form == "array"
                                     else pb.dictionary([(b"/D", array)])))
            else:
                dests_dict.append((pb.name(dname), array))
            named_labels.append({"name": dname, "form": form, "entryForm": entry_form,
                                 "targetPage": page_numbers[target], "targetIdentity": ident[target]})
        tree_entries.sort(key=lambda entry: entry[0].encode("ascii"))
        names_tree = pdf.add(pb.dictionary([
            (b"/Limits", pb.array([pb.literal(tree_entries[0][0]), pb.literal(tree_entries[-1][0])])),
            (b"/Names", pb.array([part for dname, value in tree_entries
                                  for part in (pb.literal(dname), value)]))]))
        names = pdf.add(pb.dictionary([(b"/Dests", pb.ref(names_tree))]))
        dests = pdf.add(pb.dictionary(dests_dict))
        label["namedDestinations"] = named_labels

        # The links.
        link_labels = []
        per_page = {}
        for page, form, target, text in REFS_LINKS:
            position = per_page.get(page, 0)
            per_page[page] = position + 1
            y = A4[1] - 56 - (position + 1) * 14
            rect = (44, y - 4, 330, y + 11)
            number = page_numbers[page]
            if form == "dest":
                pairs = [(b"/Dest", dest(page_numbers[target], b"/Fit"))]
            elif form == "goto":
                pairs = [(b"/A", pb.dictionary([(b"/S", b"/GoTo"),
                                                (b"/D", dest(page_numbers[target], b"/Fit"))]))]
            elif form == "destString":
                pairs = [(b"/Dest", pb.literal("dest-beta"))]
            elif form == "destName":
                pairs = [(b"/Dest", b"/old-delta")]
            elif form == "gotoNamed":
                pairs = [(b"/A", pb.dictionary([(b"/S", b"/GoTo"), (b"/D", pb.literal("dest-gamma"))]))]
            else:
                pairs = [(b"/A", pb.dictionary([
                    (b"/S", b"/GoToR"), (b"/F", pb.literal(name)),
                    (b"/D", pb.array([pb.num(target), b"/Fit"]))]))]
            annot = link(doc, number, rect, pairs)
            page_extra[number].append(annot)
            entry = {"page": page, "object": annot, "form": form, "rect": box(*rect),
                     "targetIdentity": ident[target]}
            if form == "goToR":
                entry.update({"file": name, "pageNumber": target})
            elif form in ("destString", "destName", "gotoNamed"):
                entry["name"] = {"destString": "dest-beta", "destName": "old-delta",
                                 "gotoNamed": "dest-gamma"}[form]
            else:
                entry["targetPage"] = page_numbers[target]
            link_labels.append(entry)
        label["links"] = link_labels

        # The outline: two levels, each item titled with the identity of the page it targets.
        def item(target, view, form="dest", children=(), is_open=False):
            return (ident[target], page_numbers[target], dest(page_numbers[target], *view), form,
                    list(children), is_open, ident[target])
        outline, outline_label = outline_root(doc, [
            item(0, (b"/Fit",), children=[item(1, (b"/XYZ", b"null", 700, b"null")),
                                          item(2, (b"/FitH", 600))], is_open=True),
            item(4, (b"/Fit",), children=[item(5, (b"/XYZ", 72, 500, 0)),
                                          item(7, (b"/Fit",), form="action")], is_open=False),
            item(11, (b"/Fit",)),
        ])
        label["outline"] = outline_label

        # /PageLabels: three ranges.
        page_labels = pb.dictionary([(b"/Nums", pb.array([
            b"0", pb.dictionary([(b"/S", b"/r")]),
            b"3", pb.dictionary([(b"/S", b"/D"), (b"/St", b"1")]),
            b"9", pb.dictionary([(b"/S", b"/D"), (b"/P", pb.literal("A-")), (b"/St", b"1")])]))])
        label["pageLabelRanges"] = [{"start": 0, "style": "r"}, {"start": 3, "style": "D", "first": 1},
                                    {"start": 9, "style": "D", "prefix": "A-", "first": 1}]

        # The structure tree: a Document element and one P per page, every element with /Pg.
        struct_root = pdf.reserve()
        document = pdf.reserve()
        elements = []
        for index, number in enumerate(page_numbers):
            elements.append(pdf.add(pb.dictionary([
                (b"/Type", b"/StructElem"), (b"/S", b"/P"), (b"/P", pb.ref(document)),
                (b"/Pg", pb.ref(number)), (b"/K", b"0")])))
        pdf.set(document, pb.dictionary([
            (b"/Type", b"/StructElem"), (b"/S", b"/Document"), (b"/P", pb.ref(struct_root)),
            (b"/Pg", pb.ref(page_numbers[0])), (b"/K", pb.array([pb.ref(e) for e in elements]))]))
        parent_tree = pdf.add(pb.dictionary([(b"/Nums", pb.array(
            [part for index, element in enumerate(elements)
             for part in (pb.num(index), pb.array([pb.ref(element)]))]))]))
        pdf.set(struct_root, pb.dictionary([
            (b"/Type", b"/StructTreeRoot"), (b"/K", pb.ref(document)),
            (b"/ParentTree", pb.ref(parent_tree)), (b"/ParentTreeNextKey", pb.num(REFS_PAGES))]))
        label["structure"] = ([{"object": document, "type": "Document", "pageIdentity": ident[0]}]
                              + [{"object": element, "type": "P", "mcid": 0, "pageIdentity": ident[i]}
                                 for i, element in enumerate(elements)])

        # Two article threads.
        threads = []
        thread_labels = []
        beads_by_page = {number: [] for number in page_numbers}
        for thread_title, pages in REFS_THREADS:
            thread = pdf.reserve()
            beads = [pdf.reserve() for _ in pages]
            for position, (bead, page) in enumerate(zip(beads, pages)):
                pairs = [(b"/Type", b"/Bead")]
                if position == 0:
                    pairs.append((b"/T", pb.ref(thread)))
                pairs += [(b"/N", pb.ref(beads[(position + 1) % len(beads)])),
                          (b"/V", pb.ref(beads[position - 1])),
                          (b"/P", pb.ref(page_numbers[page])),
                          (b"/R", pb.rect(48, 120, A4[0] - 48, 360))]
                pdf.set(bead, pb.dictionary(pairs))
                beads_by_page[page_numbers[page]].append(bead)
            pdf.set(thread, pb.dictionary([
                (b"/Type", b"/Thread"), (b"/F", pb.ref(beads[0])),
                (b"/I", pb.dictionary([(b"/Title", pb.utf16(thread_title))]))]))
            threads.append(thread)
            thread_labels.append({"object": thread, "title": thread_title,
                                  "beads": [{"object": bead, "pageIdentity": ident[page]}
                                            for bead, page in zip(beads, pages)]})
        label["threads"] = thread_labels
        label["openAction"] = {"targetPage": page_numbers[REFS_OPEN_ACTION],
                               "targetIdentity": ident[REFS_OPEN_ACTION]}
        catalog_extra = [
            (b"/Outlines", pb.ref(outline)), (b"/PageMode", b"/UseOutlines"),
            (b"/Names", pb.ref(names)), (b"/Dests", pb.ref(dests)),
            (b"/PageLabels", page_labels),
            (b"/StructTreeRoot", pb.ref(struct_root)),
            (b"/MarkInfo", pb.dictionary([(b"/Marked", b"true")])),
            (b"/Threads", pb.array([pb.ref(t) for t in threads])),
            (b"/OpenAction", dest(page_numbers[REFS_OPEN_ACTION], b"/Fit"))]

    for index, number in enumerate(page_numbers):
        extra = []
        if page_extra[number]:
            extra.append((b"/Annots", pb.array([pb.ref(a) for a in page_extra[number]])))
        if with_refs:
            extra.append((b"/StructParents", pb.num(index)))
            if beads_by_page[number]:
                extra.append((b"/B", pb.array([pb.ref(b) for b in beads_by_page[number]])))
        pdf.set(number, page_dict(pages_number, A4, doc.contents(page_ops[number]),
                                  doc.resources(), extra))
    pdf.set(pages_number, pages_dict(page_numbers, REFS_PAGES))
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number))]
                                    + catalog_extra))
    return doc.serialize(catalog, label)


@generator("refs-every-kind.pdf")
def refs_every_kind():
    """Twelve identity pages and every kind of reference to a page plan D7-2 names: an outline to
    pages by reference, named destinations in both forms, links by /Dest and by name, a GoToR to
    itself, page labels in three ranges, a structure tree with /Pg on every element, two article
    threads and an /OpenAction. Each reference's label entry names the identity of its target."""
    return _refs_document("refs-every-kind.pdf", "RawPage corpus: every reference to a page", True)


@generator("refs-none.pdf")
def refs_none():
    """The control: refs-every-kind.pdf's twelve pages, with nothing in the file naming any of
    them -- no outline, destination, link, label, structure, thread or open action."""
    return _refs_document("refs-none.pdf", "RawPage corpus: no reference to any page", False)


# ---------------------------------------------------------------------------------------------
# refs-by-index.pdf -- a producer's errors a repair must not "fix"
# ---------------------------------------------------------------------------------------------

@generator("refs-by-index.pdf")
def refs_by_index():
    """An outline item whose /Dest names a page by INTEGER -- legal only in a remote destination
    (ISO 32000-1 12.3.2.2) -- and a link whose /Dest names a page object no /Kids lists. Beside each,
    a control naming a page properly. Neither error is ours to correct: D7-2 repairs what names
    pages by index or position in the forms the standard defines, and a repair that "fixed" these
    would be inventing a target."""
    doc = Doc("refs-by-index.pdf", "RawPage corpus: references by index and outside the document")
    pdf = doc.pdf
    count = 6
    pages_number = pdf.reserve()
    page_numbers = [pdf.reserve() for _ in range(count)]
    ident = [identity("X", i + 1) for i in range(count)]
    orphan = pdf.reserve()

    link_good = link(doc, page_numbers[0], (44, 770, 330, 785), [(b"/Dest", dest(page_numbers[3], b"/Fit"))])
    link_outside = link(doc, page_numbers[0], (44, 756, 330, 771), [(b"/Dest", dest(orphan, b"/Fit"))])
    for index, number in enumerate(page_numbers):
        lines = ["refs-by-index.pdf - page %d of %d" % (index + 1, count)]
        if index == 0:
            lines += ["link 1: by reference, to the fourth page (the control)",
                      "link 2: to a page object that is not in the page tree"]
        ops = captions(A4, lines) + [identity_ops(ident[index], A4)]
        extra = [(b"/Annots", pb.array([pb.ref(link_good), pb.ref(link_outside)]))] if index == 0 else []
        pdf.set(number, page_dict(pages_number, A4, doc.contents(ops), doc.resources(), extra))
        doc.note_page(index, number, ident[index], A4)
    # The page outside the document: a page dictionary with a /Parent, that no /Kids lists. It
    # names itself so that a reader which somehow showed it would say so.
    outside = identity("X", 99)
    pdf.set(orphan, page_dict(pages_number, A4, doc.contents(
        captions(A4, ["refs-by-index.pdf - a page object outside the page tree"])
        + [identity_ops(outside, A4)]), doc.resources()))
    pdf.set(pages_number, pages_dict(page_numbers, count))

    outline, outline_label = outline_root(doc, [
        (ident[0], page_numbers[0], dest(page_numbers[0], b"/Fit"), "dest", [], False, ident[0]),
        ("integer destination 2", None, pb.array([b"2", b"/Fit"]), "dest", [], False, None),
    ])
    outline_label[1]["integerPage"] = 2
    outline_label[1]["note"] = ("an integer page in a LOCAL destination; no target is claimed, and "
                                "a repair must leave it as written")
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number)),
                                     (b"/Outlines", pb.ref(outline))]))
    return doc.serialize(catalog, {
        "outline": outline_label,
        "links": [{"page": 0, "object": link_good, "form": "dest", "targetPage": page_numbers[3],
                   "targetIdentity": ident[3]},
                  {"page": 0, "object": link_outside, "form": "dest", "targetPage": orphan,
                   "targetIdentity": None, "outsidePageTree": True, "orphanShows": outside}]})


# ---------------------------------------------------------------------------------------------
# inherit-resources.pdf
# ---------------------------------------------------------------------------------------------

def _checker_image(doc):
    """An 8 x 8 grey checkerboard, the one image two pages share."""
    pixels = bytes((0x30 if (x + y) % 2 else 0xD0) for y in range(8) for x in range(8))
    data, filter_name = pb.flate(pixels)
    return doc.pdf.add_stream([(b"/Type", b"/XObject"), (b"/Subtype", b"/Image"), (b"/Width", b"8"),
                               (b"/Height", b"8"), (b"/ColorSpace", b"/DeviceGray"),
                               (b"/BitsPerComponent", b"8"), (b"/Filter", filter_name)], data)


@generator("inherit-resources.pdf")
def inherit_resources():
    """Five pages that carry NO /Resources, /MediaBox or /CropBox of their own: each is inherited
    through up to three levels of intermediate /Pages nodes (four of the five sit under three, one
    under two), each attribute from a different level, and /Rotate is inherited too except on one
    page that overrides it. Two pages inherit one resource dictionary, and so share one font and
    one image. A page copied from here without materializing what it inherited draws with no font
    (plan D7-4); pages-900.pdf, whose pages carry their own, is the control."""
    doc = Doc("inherit-resources.pdf", "RawPage corpus: inherited page attributes")
    pdf = doc.pdf
    image = _checker_image(doc)
    shared = pdf.add(pb.dictionary([(b"/Font", doc.fonts()), (b"/XObject", pb.dictionary(
        [(b"/Im1", pb.ref(image))]))]))
    root_resources = pdf.add(pb.dictionary([(b"/Font", pb.dictionary([(b"/F2", pb.ref(doc.bold))]))]))
    letter_resources = pdf.add(pb.dictionary([(b"/Font", doc.fonts())]))

    root = pdf.reserve()
    l1a, l2a, l3a = pdf.reserve(), pdf.reserve(), pdf.reserve()
    l1b, l2b, l3b = pdf.reserve(), pdf.reserve(), pdf.reserve()
    p1, p2, p3, p4, p5 = [pdf.reserve() for _ in range(5)]
    crop_a = (20, 20, A4[0] - 20, A4[1] - 20)
    crop_b = (30, 30, LETTER[0] - 30, LETTER[1] - 30)

    def draw(ident, media, lines, picture):
        ops = captions(media, lines) + [identity_ops(ident, media)]
        if picture:
            ops.append(b"q 64 0 0 64 %s 200 cm /Im1 Do Q" % pb.num(media[0] / 2 - 32))
        return doc.contents(ops)

    pages = [
        (p1, l3a, A4, crop_a, 90, shared, [root, l1a, l2a, l3a], True),
        (p2, l3a, A4, crop_a, 90, shared, [root, l1a, l2a, l3a], True),
        (p3, l2a, A4, crop_a, 90, root_resources, [root, l1a, l2a], False),
        (p4, l3b, LETTER, crop_b, 180, letter_resources, [root, l1b, l2b, l3b], False),
        (p5, l3b, LETTER, crop_b, 0, letter_resources, [root, l1b, l2b, l3b], False),
    ]
    for index, (number, parent, media, crop, rotate, resources, path, picture) in enumerate(pages):
        ident = identity("H", index + 1)
        lines = ["inherit-resources.pdf - page %d of 5" % (index + 1),
                 "no /Resources, /MediaBox or /CropBox of its own; %d levels above it"
                 % (len(path) - 1)]
        if picture:
            lines.append("draws the image and the font it shares with one other page")
        extra = [(b"/Rotate", b"0")] if number == p5 else []
        pdf.set(number, pb.dictionary([(b"/Type", b"/Page"), (b"/Parent", pb.ref(parent)),
                                       (b"/Contents", pb.ref(draw(ident, media, lines, picture)))]
                                      + extra))
        found_at = {"mediaBox": root if media == A4 else l1b,
                    "cropBox": l2a if media == A4 else l3b,
                    "resources": {shared: l3a, root_resources: root, letter_resources: l1b}[resources],
                    "rotate": number if number == p5 else (l1a if media == A4 else l2b)}
        doc.note_page(index, number, ident, media, rotate=rotate, extra={
            "cropBox": box(*crop), "resources": resources, "path": path, "inheritedFrom": found_at,
            "ownKeys": ["Rotate"] if number == p5 else []})

    pdf.set(l3a, pages_dict([p1, p2], 2, [(b"/Parent", pb.ref(l2a)), (b"/Resources", pb.ref(shared))]))
    pdf.set(l2a, pages_dict([l3a, p3], 3, [(b"/Parent", pb.ref(l1a)), (b"/CropBox", pb.rect(*crop_a))]))
    pdf.set(l1a, pages_dict([l2a], 3, [(b"/Parent", pb.ref(root)), (b"/Rotate", b"90")]))
    pdf.set(l3b, pages_dict([p4, p5], 2, [(b"/Parent", pb.ref(l2b)), (b"/CropBox", pb.rect(*crop_b))]))
    pdf.set(l2b, pages_dict([l3b], 2, [(b"/Parent", pb.ref(l1b)), (b"/Rotate", b"180")]))
    pdf.set(l1b, pages_dict([l2b], 2, [(b"/Parent", pb.ref(root)),
                                       (b"/MediaBox", pb.rect(0, 0, LETTER[0], LETTER[1])),
                                       (b"/Resources", pb.ref(letter_resources))]))
    pdf.set(root, pages_dict([l1a, l1b], 5, [(b"/MediaBox", pb.rect(0, 0, A4[0], A4[1])),
                                             (b"/Resources", pb.ref(root_resources))]))
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(root))]))
    return doc.serialize(catalog, {
        "nodes": {"root": root, "l1a": l1a, "l2a": l2a, "l3a": l3a, "l1b": l1b, "l2b": l2b, "l3b": l3b},
        "shared": {"resources": shared, "font": doc.bold, "image": image,
                   "pageIdentities": [identity("H", 1), identity("H", 2)]}})


# ---------------------------------------------------------------------------------------------
# annots-on-pages.pdf
# ---------------------------------------------------------------------------------------------

@generator("annots-on-pages.pdf")
def annots_on_pages():
    """Four pages, each carrying a note with its pop-up, a reply to the note, a link to the next
    page (the last to the first) and the widget of a text field: what comes with a page that is
    moved, copied or deleted (plan D7-4)."""
    doc = Doc("annots-on-pages.pdf", "RawPage corpus: annotations on every page")
    pdf = doc.pdf
    count = 4
    pages_number = pdf.reserve()
    page_numbers = [pdf.reserve() for _ in range(count)]
    ident = [identity("AN", i + 1) for i in range(count)]
    note_ap = note_appearance(doc)
    reply_ap = note_appearance(doc, b"0.6 0.8 1")
    fields = []
    for index, number in enumerate(page_numbers):
        note, popup, reply = pdf.reserve(), pdf.reserve(), pdf.reserve()
        target = (index + 1) % count
        pdf.set(note, pb.dictionary([
            (b"/Type", b"/Annot"), (b"/Subtype", b"/Text"), (b"/Rect", pb.rect(60, 700, 80, 720)),
            (b"/P", pb.ref(number)), (b"/NM", pb.literal("an-note-%d" % (index + 1))),
            (b"/T", pb.literal("Reviewer")), (b"/Contents", pb.literal("note on page %d" % (index + 1))),
            (b"/F", b"28"), (b"/Popup", pb.ref(popup)), (b"/AP", pb.dictionary([(b"/N", pb.ref(note_ap))]))]))
        pdf.set(popup, pb.dictionary([
            (b"/Type", b"/Annot"), (b"/Subtype", b"/Popup"), (b"/Rect", pb.rect(90, 620, 290, 720)),
            (b"/P", pb.ref(number)), (b"/Parent", pb.ref(note)), (b"/Open", b"false"), (b"/F", b"28")]))
        pdf.set(reply, pb.dictionary([
            (b"/Type", b"/Annot"), (b"/Subtype", b"/Text"), (b"/Rect", pb.rect(60, 670, 80, 690)),
            (b"/P", pb.ref(number)), (b"/NM", pb.literal("an-reply-%d" % (index + 1))),
            (b"/T", pb.literal("Author")), (b"/Contents", pb.literal("reply on page %d" % (index + 1))),
            (b"/F", b"28"), (b"/IRT", pb.ref(note)), (b"/AP", pb.dictionary([(b"/N", pb.ref(reply_ap))]))]))
        link_number = link(doc, number, (44, 560, 330, 575),
                           [(b"/Dest", dest(page_numbers[target], b"/Fit"))])
        field_name = "field-%d" % (index + 1)
        widget_ap = appearance(doc, 200, 20, b"/Tx BMC q BT /F1 10 Tf 2 6 Td (value %d) Tj ET Q EMC"
                               % (index + 1), doc.resources())
        widget = pdf.add(pb.dictionary([
            (b"/Type", b"/Annot"), (b"/Subtype", b"/Widget"), (b"/FT", b"/Tx"),
            (b"/T", pb.literal(field_name)), (b"/V", pb.literal("value %d" % (index + 1))),
            (b"/DA", pb.literal("/Helv 10 Tf 0 g")), (b"/Rect", pb.rect(48, 500, 248, 520)),
            (b"/P", pb.ref(number)), (b"/F", b"4"), (b"/MK", pb.dictionary([(b"/BC", pb.array([b"0"]))])),
            (b"/AP", pb.dictionary([(b"/N", pb.ref(widget_ap))]))]))
        fields.append(widget)
        ops = captions(A4, ["annots-on-pages.pdf - page %d of %d" % (index + 1, count),
                            "a note, its pop-up and a reply at the top left"], top=A4[1] - 40)
        ops += [caption(48, 590, 10, "a link to the page after this one (the last page links to the first):"),
                stroke_rect(44, 556, 330, 579, grey=0.2), caption(48, 564, 10, "go to the next page"),
                caption(48, 528, 10, "the widget of the text field %s:" % field_name)]
        ops.append(identity_ops(ident[index], A4))
        annots = [note, popup, reply, link_number, widget]
        pdf.set(number, page_dict(pages_number, A4, doc.contents(ops), doc.resources(),
                                  [(b"/Annots", pb.array([pb.ref(a) for a in annots]))]))
        doc.note_page(index, number, ident[index], A4, extra={"annots": [
            {"object": note, "subtype": "Text", "role": "note", "popup": popup, "nm": "an-note-%d" % (index + 1)},
            {"object": popup, "subtype": "Popup", "role": "popup", "parent": note},
            {"object": reply, "subtype": "Text", "role": "reply", "irt": note, "nm": "an-reply-%d" % (index + 1)},
            {"object": link_number, "subtype": "Link", "role": "link", "targetPage": page_numbers[target],
             "targetIdentity": ident[target]},
            {"object": widget, "subtype": "Widget", "role": "widget", "field": field_name}]})
    pdf.set(pages_number, pages_dict(page_numbers, count))
    acroform = pdf.add(pb.dictionary([
        (b"/Fields", pb.array([pb.ref(f) for f in fields])), (b"/DA", pb.literal("/Helv 10 Tf 0 g")),
        (b"/DR", pb.dictionary([(b"/Font", pb.dictionary([(b"/Helv", pb.ref(doc.helv))]))]))]))
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number)),
                                     (b"/AcroForm", pb.ref(acroform))]))
    return doc.serialize(catalog, {"fields": ["field-%d" % (i + 1) for i in range(count)]})


# ---------------------------------------------------------------------------------------------
# merge-a.pdf, merge-b.pdf, merge-c.pdf -- three merge sources
# ---------------------------------------------------------------------------------------------

# Per source: its tag, its page sizes, its fields (top-level name -> kids or None) and its layers.
# "name" and "date" collide in all three; "address" collides between b and c as a NON-terminal
# field whose kids differ, so a rename has a /Parent chain to keep consistent (plan D7-16).
MERGE_SOURCES = {
    "a": ("MA", [A4, A4, A4], [("name", None, "Ana Lima"), ("date", None, "2026-10-01"),
                               ("a-only", None, "first source")], ["Draft", "Source A notes"]),
    "b": ("MB", [LETTER, LETTER], [("name", None, "Ben Okafor"), ("date", None, "2026-10-02"),
                                   ("address", ["street"], "1 Long Road"), ("b-only", None, "second source")],
          ["Draft", "Source B notes"]),
    "c": ("MC", [A5, A5_LANDSCAPE, LETTER, A4],
          [("name", None, "Chen Wei"), ("date", None, "2026-10-03"), ("address", ["city"], "Taipei"),
           ("c-only", None, "third source")], ["Draft", "Source C notes"]),
}


def _merge_source(key):
    tag, sizes, fields, layers = MERGE_SOURCES[key]
    name = "merge-%s.pdf" % key
    doc = Doc(name, "Merge source %s" % key.upper())
    pdf = doc.pdf
    count = len(sizes)
    pages_number = pdf.reserve()
    page_numbers = [pdf.reserve() for _ in range(count)]
    ident = [identity(tag, i + 1) for i in range(count)]

    groups = [pdf.add(pb.dictionary([(b"/Type", b"/OCG"), (b"/Name", pb.utf16(layer))])) for layer in layers]
    note_ap = note_appearance(doc)

    # Fields, every widget on the first page.
    top_fields = []
    widgets = []
    field_labels = []
    # Every widget on the first page, above the identity and below the link at any of the sizes.
    link_y = sizes[0][1] - 100
    y = sizes[0][1] - 140
    for field, kids, value in fields:
        if kids is None:
            widget = pdf.reserve()
            pdf.set(widget, pb.dictionary([
                (b"/Type", b"/Annot"), (b"/Subtype", b"/Widget"), (b"/FT", b"/Tx"),
                (b"/T", pb.literal(field)), (b"/V", pb.literal(value)),
                (b"/DA", pb.literal("/Helv 10 Tf 0 g")), (b"/Rect", pb.rect(150, y, 330, y + 16)),
                (b"/P", pb.ref(page_numbers[0])), (b"/F", b"4"),
                (b"/AP", pb.dictionary([(b"/N", pb.ref(appearance(
                    doc, 180, 16, b"/Tx BMC q BT /F1 10 Tf 2 4 Td %s Tj ET Q EMC" % pb.literal(value),
                    doc.resources())))]))]))
            top_fields.append(widget)
            widgets.append((widget, field, y))
            field_labels.append({"name": field, "object": widget, "kids": None, "value": value})
        else:
            parent = pdf.reserve()
            kid_numbers = []
            for kid in kids:
                widget = pdf.add(pb.dictionary([
                    (b"/Type", b"/Annot"), (b"/Subtype", b"/Widget"), (b"/FT", b"/Tx"),
                    (b"/Parent", pb.ref(parent)), (b"/T", pb.literal(kid)), (b"/V", pb.literal(value)),
                    (b"/DA", pb.literal("/Helv 10 Tf 0 g")), (b"/Rect", pb.rect(150, y, 330, y + 16)),
                    (b"/P", pb.ref(page_numbers[0])), (b"/F", b"4"),
                    (b"/AP", pb.dictionary([(b"/N", pb.ref(appearance(
                        doc, 180, 16, b"/Tx BMC q BT /F1 10 Tf 2 4 Td %s Tj ET Q EMC" % pb.literal(value),
                        doc.resources())))]))]))
                kid_numbers.append(widget)
                widgets.append((widget, "%s.%s" % (field, kid), y))
            pdf.set(parent, pb.dictionary([(b"/T", pb.literal(field)),
                                           (b"/Kids", pb.array([pb.ref(k) for k in kid_numbers]))]))
            top_fields.append(parent)
            field_labels.append({"name": field, "object": parent, "kids": [
                {"name": kid, "object": number} for kid, number in zip(kids, kid_numbers)], "value": value})
        y -= 24

    note = pdf.add(pb.dictionary([
        (b"/Type", b"/Annot"), (b"/Subtype", b"/Text"), (b"/Rect", pb.rect(60, 120, 80, 140)),
        (b"/P", pb.ref(page_numbers[count - 1])), (b"/NM", pb.literal("merge-%s-note" % key)),
        (b"/T", pb.literal("Reviewer")), (b"/Contents", pb.literal("a comment in source %s" % key)),
        (b"/F", b"28"), (b"/AP", pb.dictionary([(b"/N", pb.ref(note_ap))]))]))
    link_number = link(doc, page_numbers[0], (44, link_y, 330, link_y + 15),
                       [(b"/Dest", dest(page_numbers[count - 1], b"/Fit"))])

    for index, (number, media) in enumerate(zip(page_numbers, sizes)):
        lines = ["%s - source %s, page %d of %d, %s x %s pt"
                 % (name, key.upper(), index + 1, count, pb.num(media[0]).decode(), pb.num(media[1]).decode())]
        ops = captions(media, lines)
        annots = []
        properties = None
        if index == 0:
            ops.append(stroke_rect(44, link_y - 4, 330, link_y + 19, grey=0.2))
            ops.append(caption(48, link_y + 4, 10, "a link to the last page of this source"))
            for widget, field, wy in widgets:
                ops.append(caption(48, wy + 4, 10, field))
            ops.append(b"/OC /oc0 BDC " + caption(48, 120, 10, "content in the layer %s" % layers[0])
                       + b" EMC")
            ops.append(b"/OC /oc1 BDC " + caption(48, 100, 10, "content in the layer %s" % layers[1])
                       + b" EMC")
            annots = [link_number] + [w for w, _, _ in widgets]
            properties = pb.dictionary([(b"/oc0", pb.ref(groups[0])), (b"/oc1", pb.ref(groups[1]))])
        if index == count - 1:
            annots.append(note)
        ops.append(identity_ops(ident[index], media))
        extra_resources = [(b"/Properties", properties)] if properties else []
        extra = [(b"/Annots", pb.array([pb.ref(a) for a in annots]))] if annots else []
        pdf.set(number, page_dict(pages_number, media, doc.contents(ops), doc.resources(extra_resources),
                                  extra))
        doc.note_page(index, number, ident[index], media)
    pdf.set(pages_number, pages_dict(page_numbers, count))

    items = [(ident[0], page_numbers[0], dest(page_numbers[0], b"/Fit"), "dest",
              [(ident[i], page_numbers[i], dest(page_numbers[i], b"/Fit"), "dest", [], False, ident[i])
               for i in range(1, count)], True, ident[0])]
    outline, outline_label = outline_root(doc, items)
    acroform = pdf.add(pb.dictionary([
        (b"/Fields", pb.array([pb.ref(f) for f in top_fields])), (b"/DA", pb.literal("/Helv 10 Tf 0 g")),
        (b"/DR", pb.dictionary([(b"/Font", pb.dictionary([(b"/Helv", pb.ref(doc.helv))]))]))]))
    oc_properties = pb.dictionary([
        (b"/OCGs", pb.array([pb.ref(g) for g in groups])),
        (b"/D", pb.dictionary([(b"/Name", pb.literal("Default")), (b"/Order", pb.array([pb.ref(g) for g in groups])),
                               (b"/ON", pb.array([pb.ref(g) for g in groups]))]))])
    catalog = pdf.add(pb.dictionary([
        (b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number)), (b"/Outlines", pb.ref(outline)),
        (b"/AcroForm", pb.ref(acroform)), (b"/OCProperties", oc_properties)]))
    return doc.serialize(catalog, {
        "title": "Merge source %s" % key.upper(), "outline": outline_label,
        "links": [{"page": 0, "object": link_number, "form": "dest", "targetPage": page_numbers[count - 1],
                   "targetIdentity": ident[count - 1]}],
        "comments": [{"object": note, "subtype": "Text", "pageIdentity": ident[count - 1]}],
        "fields": field_labels,
        "layers": [{"object": g, "name": layer} for g, layer in zip(groups, layers)],
        "collidingFields": {"a": ["name", "date"], "b": ["name", "date", "address"],
                            "c": ["name", "date", "address"]}[key]})


@generator("merge-a.pdf")
def merge_a():
    """Merge source A: three A4 pages, an outline, a link, a comment, fields named `name`, `date`
    and `a-only`, two layers, and a /Title."""
    return _merge_source("a")


@generator("merge-b.pdf")
def merge_b():
    """Merge source B: two Letter pages; `name` and `date` collide with A's and C's, and `address`
    (one kid, `street`) collides with C's as a non-terminal field."""
    return _merge_source("b")


@generator("merge-c.pdf")
def merge_c():
    """Merge source C: four pages of four sizes -- A5, A5 landscape, Letter, A4 -- `address` with
    the kid `city`, and the same layers' shape as A and B (each with its own `Draft`)."""
    return _merge_source("c")


NOT_A_PDF = (b"This is not a PDF. It is a text file named .pdf, which a person can drop into the "
             b"merge dialog; Stage 7 must skip it and say why (plan D7-16).\n")


# ---------------------------------------------------------------------------------------------
# layers.pdf
# ---------------------------------------------------------------------------------------------

@generator("layers.pdf")
def layers():
    """Optional content: a locked group, a group with two nested children, a radio pair, a
    print-only and a screen-only group with their /AS usage, content in two groups on one page,
    and every group reached from a page through a /Properties resource entry (plan D7-9)."""
    doc = Doc("layers.pdf", "RawPage corpus: optional content")
    pdf = doc.pdf

    specs = [  # key, name, usage
        ("base", "Base drawing", None),
        ("annotations", "Annotations", None),
        ("notes", "Reviewer notes", None),
        ("markup", "Markup", None),
        ("en", "English", None),
        ("fr", "French", None),
        ("print", "Print only", ("ON", "OFF")),
        ("screen", "Screen only", ("OFF", "ON")),
    ]
    group = {}
    for key, gname, usage in specs:
        pairs = [(b"/Type", b"/OCG"), (b"/Name", pb.utf16(gname))]
        if usage is not None:
            pairs.append((b"/Usage", pb.dictionary([
                (b"/Print", pb.dictionary([(b"/PrintState", b"/" + usage[0].encode())])),
                (b"/View", pb.dictionary([(b"/ViewState", b"/" + usage[1].encode())]))])))
        group[key] = pdf.add(pb.dictionary(pairs))

    def refs(*keys):
        return pb.array([pb.ref(group[k]) for k in keys])

    order = pb.array([pb.ref(group["base"]), pb.ref(group["annotations"]), refs("notes", "markup"),
                      pb.array([pb.literal("Languages"), pb.ref(group["en"]), pb.ref(group["fr"])]),
                      pb.ref(group["print"]), pb.ref(group["screen"])])
    off = ["fr", "print"]
    usage_events = pb.array([
        pb.dictionary([(b"/Event", b"/Print"), (b"/OCGs", refs("print", "screen")),
                       (b"/Category", pb.array([b"/Print"]))]),
        pb.dictionary([(b"/Event", b"/View"), (b"/OCGs", refs("print", "screen")),
                       (b"/Category", pb.array([b"/View"]))])])
    oc_properties = pb.dictionary([
        (b"/OCGs", pb.array([pb.ref(group[k]) for k, _, _ in specs])),
        (b"/D", pb.dictionary([
            (b"/Name", pb.literal("Default")), (b"/BaseState", b"/ON"),
            (b"/OFF", refs(*off)), (b"/Order", order), (b"/RBGroups", pb.array([refs("en", "fr")])),
            (b"/Locked", refs("base")), (b"/AS", usage_events)]))])

    pages_number = pdf.reserve()
    page_numbers = [pdf.reserve(), pdf.reserve()]
    page_groups = [
        [("oc1", "base", "the base drawing: a frame"), ("oc2", "notes", "reviewer notes layer text"),
         ("oc3", "markup", "markup layer text")],
        [("en", "en", "This sentence is in English."), ("fr", "fr", "Cette phrase est en francais."),
         ("pr", "print", "drawn only when printed"), ("sc", "screen", "drawn only on screen")],
    ]
    for index, number in enumerate(page_numbers):
        ident = identity("L", index + 1)
        ops = captions(A4, ["layers.pdf - page %d of 2" % (index + 1)])
        y = 700
        for prop, key, text in page_groups[index]:
            drawn = caption(72, y, 12, text)
            if key == "base":
                drawn = stroke_rect(36, 36, A4[0] - 36, A4[1] - 36, grey=0.5, line=2) + b" " + drawn
            ops.append(b"/OC /%s BDC " % prop.encode() + drawn + b" EMC")
            y -= 24
        ops.append(identity_ops(ident, A4))
        properties = pb.dictionary([(b"/" + prop.encode(), pb.ref(group[key]))
                                    for prop, key, _ in page_groups[index]])
        pdf.set(number, page_dict(pages_number, A4, doc.contents(ops),
                                  doc.resources([(b"/Properties", properties)])))
        doc.note_page(index, number, ident, A4, extra={"properties": {
            prop: group[key] for prop, key, _ in page_groups[index]}})
    pdf.set(pages_number, pages_dict(page_numbers, 2))
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number)),
                                     (b"/OCProperties", oc_properties)]))
    parents = {"notes": "annotations", "markup": "annotations"}
    return doc.serialize(catalog, {"groups": [
        {"key": key, "object": group[key], "name": gname, "parent": parents.get(key),
         "orderLabel": "Languages" if key in ("en", "fr") else None,
         "locked": key == "base", "defaultOn": key not in off,
         "radioGroup": 0 if key in ("en", "fr") else None,
         "usage": None if usage is None else {"print": usage[0], "view": usage[1]},
         "pages": [i for i, groups in enumerate(page_groups) if any(k == key for _, k, _ in groups)]}
        for key, gname, usage in specs]})


# ---------------------------------------------------------------------------------------------
# tagged-headings.pdf and untagged-headings.pdf
# ---------------------------------------------------------------------------------------------

HEADING_SIZES = {1: 20, 2: 16, 3: 13}
BODY_SIZE = 11
HEADINGS_TEXT = [  # per page: (level or 0 for a paragraph, text or a seed)
    [(1, "1 Assembly overview"), (0, 101), (2, "1.1 Moving pages"), (0, 102), (3, "1.1.1 Page maps"),
     (0, 103)],
    [(2, "1.2 Copying pages"), (0, 104), (3, "1.2.1 Shared resources"), (0, 105), (1, "2 New documents"),
     (0, 106)],
    [(2, "2.1 Splitting"), (0, 107), (2, "2.2 Merging"), (0, 108), (3, "2.2.1 Field names"), (0, 109)],
]


def _headings_document(name, title, tagged):
    doc = Doc(name, title)
    pdf = doc.pdf
    count = len(HEADINGS_TEXT)
    pages_number = pdf.reserve()
    page_numbers = [pdf.reserve() for _ in range(count)]
    struct_root = pdf.reserve() if tagged else None
    document = pdf.reserve() if tagged else None
    elements = []
    parent_tree = []
    headings = []
    for index, number in enumerate(page_numbers):
        ident = identity("TG", index + 1)
        ops = []
        mcid = 0
        page_elements = []
        y = A4[1] - 72
        for level, value in HEADINGS_TEXT[index]:
            if level:
                size = HEADING_SIZES[level]
                lines = [value]
                bold = True
            else:
                size = BODY_SIZE
                lines = wrap(pb.words_for(value, 34), size, A4[0] - 96)
                bold = False
            y -= size * (1.2 if level else 0.6)
            run = []
            for line in lines:
                run.append(caption(48, y, size, line, b"/F2" if bold else b"/F1"))
                y -= size * 1.35
            y -= size * 0.6
            body = b"\n".join(run)
            tag = ("H%d" % level) if level else "P"
            if tagged:
                ops.append(b"/%s << /MCID %d >> BDC " % (tag.encode(), mcid) + body + b" EMC")
                element = pdf.add(pb.dictionary([
                    (b"/Type", b"/StructElem"), (b"/S", b"/" + tag.encode()), (b"/P", pb.ref(document)),
                    (b"/Pg", pb.ref(number)), (b"/K", pb.num(mcid))]))
                elements.append(element)
                page_elements.append(element)
                mcid += 1
            else:
                ops.append(body)
            if level:
                headings.append({"level": level, "text": value, "pageIndex": index, "pageIdentity": ident,
                                 "fontSize": size, "bold": True,
                                 "object": page_elements[-1] if tagged else None})
        footer = footer_identity_ops(ident, A4)
        ops.append(artifact(footer, "Footer") if tagged else footer)
        parent_tree.append(page_elements)
        extra = [(b"/StructParents", pb.num(index))] if tagged else []
        pdf.set(number, page_dict(pages_number, A4, doc.contents(ops), doc.resources(), extra))
        doc.note_page(index, number, ident, A4)
    pdf.set(pages_number, pages_dict(page_numbers, count))
    root_extra = []
    if tagged:
        pdf.set(document, pb.dictionary([
            (b"/Type", b"/StructElem"), (b"/S", b"/Document"), (b"/P", pb.ref(struct_root)),
            (b"/K", pb.array([pb.ref(e) for e in elements]))]))
        tree = pdf.add(pb.dictionary([(b"/Nums", pb.array(
            [part for index, page_elements in enumerate(parent_tree)
             for part in (pb.num(index), pb.array([pb.ref(e) for e in page_elements]))]))]))
        pdf.set(struct_root, pb.dictionary([
            (b"/Type", b"/StructTreeRoot"), (b"/K", pb.ref(document)), (b"/ParentTree", pb.ref(tree)),
            (b"/ParentTreeNextKey", pb.num(count))]))
        root_extra = [(b"/StructTreeRoot", pb.ref(struct_root)),
                      (b"/MarkInfo", pb.dictionary([(b"/Marked", b"true")])), (b"/Lang", pb.literal("en-GB"))]
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number))]
                                    + root_extra))

    def nest(flat):
        out, stack = [], []
        for heading in flat:
            node = {"level": heading["level"], "text": heading["text"],
                    "pageIdentity": heading["pageIdentity"], "children": []}
            while stack and stack[-1]["level"] >= node["level"]:
                stack.pop()
            (stack[-1]["children"] if stack else out).append(node)
            stack.append(node)
        return out

    data, label = doc.serialize(catalog, {
        "identity": {"pattern": IDENTITY_PATTERN, "font": "Helvetica", "size": FOOTER_SIZE,
                     "placement": "footer"},
        "tagged": tagged, "bodySize": BODY_SIZE,
        "headingSizes": {str(k): v for k, v in HEADING_SIZES.items()},
        "headingPattern": r"^[0-9]+(\.[0-9]+)* ",
        "headings": headings, "expectedOutline": nest(headings),
        "paragraphs": sum(1 for page in HEADINGS_TEXT for level, _ in page if not level)})
    return data, label


@generator("tagged-headings.pdf")
def tagged_headings():
    """A tagged document of three pages: H1, H2 and H3 headings with paragraphs between, each a
    structure element with /Pg and an MCID, its identity a 9-point footer marked as a pagination
    artifact -- the source D7-10's from-structure outline generator reads, and the tags pane's."""
    return _headings_document("tagged-headings.pdf", "RawPage corpus: tagged headings", True)


@generator("untagged-headings.pdf")
def untagged_headings():
    """The control: tagged-headings.pdf's text at the same sizes and positions with no structure at
    all -- the source the layout path (font size, style, a text pattern) has to find headings in."""
    return _headings_document("untagged-headings.pdf", "RawPage corpus: untagged headings", False)


# ---------------------------------------------------------------------------------------------
# split-blanks.pdf
# ---------------------------------------------------------------------------------------------

# Per page: (kind, heading). Kinds: "ink" draws its identity and text; "absent" has no /Contents;
# "empty" a content stream that paints nothing; "white" draws text and a rectangle in white.
SPLIT_PAGES = [("ink", "Part 1: Intake"), ("ink", None), ("absent", None), ("ink", "Part 2: Review"),
               ("ink", None), ("empty", None), ("white", None), ("ink", "Part 3: Archive"), ("ink", None)]


@generator("split-blanks.pdf")
def split_blanks():
    """Nine pages in three parts, each part opening on a page whose first line matches `Part N:`
    and named by a top-level bookmark, separated by pages with no ink: one with no /Contents, one
    whose content stream paints nothing, and one that draws text and a rectangle in WHITE -- blank
    to the eye, not to the content box."""
    doc = Doc("split-blanks.pdf", "RawPage corpus: blank pages and a recurring heading")
    pdf = doc.pdf
    count = len(SPLIT_PAGES)
    pages_number = pdf.reserve()
    page_numbers = [pdf.reserve() for _ in range(count)]
    starts = []
    for index, (number, (kind, heading)) in enumerate(zip(page_numbers, SPLIT_PAGES)):
        ident = identity("SB", index + 1)
        contents = None
        shown = None
        if kind in ("ink", "white"):
            ops = []
            if heading:
                ops.append(caption(48, A4[1] - 80, 20, heading, b"/F2"))
                starts.append(index)
            lines = wrap(pb.words_for(200 + index, 40), 10, A4[0] - 96)
            ops += [caption(48, A4[1] - 120 - n * 14, 10, line) for n, line in enumerate(lines)]
            if kind == "white":
                ops = [b"1 g"] + ops + [fill_rect(100, 150, 300, 250, 1)]
                ops.append(identity_ops(ident, A4, grey=1))
            else:
                ops.append(identity_ops(ident, A4))
                shown = ident
            contents = doc.contents(ops)
        elif kind == "empty":
            contents = pdf.add_stream([], b"q Q\n")
        pdf.set(number, page_dict(pages_number, A4, contents, doc.resources()))
        doc.note_page(index, number, shown, A4, extra={
            "kind": kind, "heading": heading, "blankToEye": kind != "ink",
            "contentBoxEmpty": kind in ("absent", "empty"),
            "drawnIdentity": ident if kind in ("ink", "white") else None})
    pdf.set(pages_number, pages_dict(page_numbers, count))
    items = [(SPLIT_PAGES[i][1], page_numbers[i], dest(page_numbers[i], b"/Fit"), "dest", [], False,
              identity("SB", i + 1)) for i in starts]
    outline, outline_label = outline_root(doc, items)
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number)),
                                     (b"/Outlines", pb.ref(outline))]))
    return doc.serialize(catalog, {"headingPattern": r"^Part [0-9]+:", "partStarts": starts,
                                   "outline": outline_label})


# ---------------------------------------------------------------------------------------------
# boxes.pdf
# ---------------------------------------------------------------------------------------------

@generator("boxes.pdf")
def boxes():
    """Page 1: all five boxes set, nested, each outlined in its own grey. Page 2: a /CropBox that
    reaches outside the /MediaBox on three sides. Page 3: wide white margins around a known content
    box -- a filled rectangle the identity sits inside, so the union of the page's object bounds IS
    that rectangle, independent of the font the engine substitutes. cropbox-margins.pdf is the
    control (Stage 4)."""
    doc = Doc("boxes.pdf", "RawPage corpus: page boxes")
    pdf = doc.pdf
    pages_number = pdf.reserve()
    page_numbers = [pdf.reserve() for _ in range(3)]

    five = {"MediaBox": (0, 0, LETTER[0], LETTER[1]), "CropBox": (18, 18, 594, 774),
            "BleedBox": (27, 27, 585, 765), "TrimBox": (36, 36, 576, 756), "ArtBox": (72, 72, 540, 720)}
    greys = {"CropBox": 0.75, "BleedBox": 0.6, "TrimBox": 0.45, "ArtBox": 0.3}
    ops = [caption(84, 700, 10, "boxes.pdf - page 1 of 3: Media, Crop, Bleed, Trim and Art boxes,"),
           caption(84, 686, 10, "each outlined, the ArtBox innermost")]
    for key, grey in greys.items():
        ops.append(stroke_rect(*five[key], grey=grey, line=1))
    ops.append(identity_ops(identity("BX", 1), LETTER))
    pdf.set(page_numbers[0], page_dict(pages_number, LETTER, doc.contents(ops), doc.resources(),
                                       [(pb.name(k), pb.rect(*v)) for k, v in five.items() if k != "MediaBox"]))
    doc.note_page(0, page_numbers[0], identity("BX", 1), LETTER,
                  extra={"boxes": {k: box(*v) for k, v in five.items()}})

    crop = (-40, 200, 400, 900)
    clipped = (0, 200, 400, A4[1])
    ops = captions(A4, ["boxes.pdf - page 2 of 3: the CropBox reaches outside the MediaBox"], top=760)
    ops.append(identity_ops(identity("BX", 2), A4))
    pdf.set(page_numbers[1], page_dict(pages_number, A4, doc.contents(ops), doc.resources(),
                                       [(b"/CropBox", pb.rect(*crop))]))
    doc.note_page(1, page_numbers[1], identity("BX", 2), A4,
                  extra={"boxes": {"MediaBox": media_box(A4), "CropBox": box(*crop)},
                         "cropBoxClippedToMedia": box(*clipped)})

    cx, cy = A4[0] / 2, A4[1] / 2
    content = (cx - 130, cy - 90, cx + 130, cy + 90)
    ops = [fill_rect(*content, 0.85), identity_ops(identity("BX", 3), A4)]
    pdf.set(page_numbers[2], page_dict(pages_number, A4, doc.contents(ops), doc.resources()))
    doc.note_page(2, page_numbers[2], identity("BX", 3), A4, extra={"contentBox": box(*content)})
    pdf.set(pages_number, pages_dict(page_numbers, 3))
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number))]))
    return doc.serialize(catalog, {})


# ---------------------------------------------------------------------------------------------
# decorated-elsewhere.pdf
# ---------------------------------------------------------------------------------------------

ROOT_HALF = math.sqrt(0.5)          # cos 45 = sin 45, exactly as IEEE 754 rounds a square root


@generator("decorated-elsewhere.pdf")
def decorated_elsewhere():
    """Two pages carrying a header and a watermark in the shape Acrobat writes them -- a form
    XObject drawn from an appended content stream inside `/Artifact << /Type /Pagination /Subtype
    /Header >> BDC`, the form carrying `/PieceInfo /ADBE_CompoundType` -- and no
    `/RawPageDecoration`. RawPage must neither list nor remove either (plan D7-7)."""
    doc = Doc("decorated-elsewhere.pdf", "RawPage corpus: decorations by another producer")
    pdf = doc.pdf
    pages_number = pdf.reserve()
    page_numbers = [pdf.reserve(), pdf.reserve()]
    gstate = pdf.add(pb.dictionary([(b"/Type", b"/ExtGState"), (b"/ca", b"0.3"), (b"/CA", b"0.3")]))
    form_resources = pb.dictionary([(b"/Font", pb.dictionary([(b"/C0_0", pb.ref(doc.helv)),
                                                              (b"/C0_1", pb.ref(doc.bold))]))])
    decorations = []

    def piece_info(private):
        return pb.dictionary([(b"/ADBE_CompoundType", pb.dictionary([
            (b"/DocSettings", pb.dictionary([(b"/Private", b"/" + private)])),
            (b"/LastModified", pb.literal(pb.FIXED_DATE)), (b"/Private", b"/" + private)]))])

    for index, number in enumerate(page_numbers):
        ident = identity("DE", index + 1)
        body = doc.contents(captions(A4, ["decorated-elsewhere.pdf - page %d of 2" % (index + 1),
                                          "a header at the top and a watermark across the page,",
                                          "both in the shape another producer writes them"], top=700)
                            + [identity_ops(ident, A4)])
        header_text = "Confidential draft - page %d of 2" % (index + 1)
        header_form = pdf.add_stream([
            (b"/Type", b"/XObject"), (b"/Subtype", b"/Form"), (b"/BBox", pb.rect(0, 0, A4[0], 40)),
            (b"/Resources", form_resources), (b"/PieceInfo", piece_info(b"Header")),
            (b"/LastModified", pb.literal(pb.FIXED_DATE))],
            b"BT /C0_0 9 Tf 0 g 1 0 0 1 %s 14 Tm %s Tj ET\n"
            % (pb.num((A4[0] - width(header_text, 9)) / 2), pb.literal(header_text)))
        mark_width = width("DRAFT", 120, bold=True)
        c = ROOT_HALF
        tx = A4[0] / 2 - c * mark_width / 2 + c * 120 * 0.35
        ty = A4[1] / 2 - c * mark_width / 2 - c * 120 * 0.35
        watermark_form = pdf.add_stream([
            (b"/Type", b"/XObject"), (b"/Subtype", b"/Form"), (b"/BBox", pb.rect(0, 0, A4[0], A4[1])),
            (b"/Resources", form_resources), (b"/PieceInfo", piece_info(b"Watermark")),
            (b"/LastModified", pb.literal(pb.FIXED_DATE))],
            b"BT /C0_1 120 Tf 0.8 0.1 0.1 rg %s %s %s %s %s %s Tm (DRAFT) Tj ET\n"
            % (pb.num(c), pb.num(c), pb.num(-c), pb.num(c), pb.num(tx), pb.num(ty)))
        header = pdf.add_stream([], b"/Artifact << /Attached [ /Top ] /Type /Pagination /Subtype /Header >> "
                                    b"BDC q 1 0 0 1 0 %s cm /Xi0 Do Q EMC\n" % pb.num(A4[1] - 40))
        watermark = pdf.add_stream([], b"/Artifact << /Type /Pagination /Subtype /Watermark >> "
                                       b"BDC q /GS0 gs /Xi1 Do Q EMC\n")
        resources = pb.dictionary([(b"/Font", doc.fonts()),
                                   (b"/XObject", pb.dictionary([(b"/Xi0", pb.ref(header_form)),
                                                                (b"/Xi1", pb.ref(watermark_form))])),
                                   (b"/ExtGState", pb.dictionary([(b"/GS0", pb.ref(gstate))]))])
        pdf.set(number, pb.dictionary([
            (b"/Type", b"/Page"), (b"/Parent", pb.ref(pages_number)),
            (b"/MediaBox", pb.rect(0, 0, A4[0], A4[1])), (b"/Resources", resources),
            (b"/Contents", pb.array([pb.ref(body), pb.ref(header), pb.ref(watermark)]))]))
        doc.note_page(index, number, ident, A4, extra={"contents": [body, header, watermark]})
        decorations += [{"kind": "Header", "pageIdentity": ident, "stream": header, "form": header_form,
                         "text": header_text},
                        {"kind": "Watermark", "pageIdentity": ident, "stream": watermark,
                         "form": watermark_form, "text": "DRAFT"}]
    pdf.set(pages_number, pages_dict(page_numbers, 2))
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number))]))
    return doc.serialize(catalog, {"decorations": decorations, "ours": 0})


# ---------------------------------------------------------------------------------------------
# odd-count-*.pdf
# ---------------------------------------------------------------------------------------------

ODD_COUNTS = (1, 2, 3, 5, 7, 9, 17)


def _odd_count(count):
    name = "odd-count-%d.pdf" % count
    doc = Doc(name, "RawPage corpus: %d identity pages" % count)
    pdf = doc.pdf
    tag = "C%02d" % count
    pages_number = pdf.reserve()
    page_numbers = [pdf.reserve() for _ in range(count)]
    for index, number in enumerate(page_numbers):
        ident = identity(tag, index + 1)
        ops = captions(LETTER, ["%s - page %d of %d" % (name, index + 1, count)])
        ops.append(stroke_rect(18, 18, LETTER[0] - 18, LETTER[1] - 18, grey=0.5))
        ops.append(identity_ops(ident, LETTER))
        pdf.set(number, page_dict(pages_number, LETTER, doc.contents(ops), doc.resources()))
        doc.note_page(index, number, ident, LETTER)
    pdf.set(pages_number, pages_dict(page_numbers, count))
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(pages_number))]))
    return doc.serialize(catalog, {"count": count})


for _count in ODD_COUNTS:
    FILES["odd-count-%d.pdf" % _count] = (lambda c: lambda: _odd_count(c))(_count)


# ---------------------------------------------------------------------------------------------

# The one file in assembly/ that is not a PDF and carries no label.
UNLABELLED = ("merge-not-a-pdf.pdf",)


def label_name(fixture):
    return "labels/assembly-%s.json" % fixture.replace(".pdf", "")


def generate():
    out = {}
    for name, builder in FILES.items():
        data, label = builder()
        out["assembly/" + name] = data
        out[label_name(name)] = label
    out["assembly/merge-not-a-pdf.pdf"] = NOT_A_PDF
    return out


if __name__ == "__main__":
    for relative, data in generate().items():
        target = ROOT / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        print("wrote %-38s %9d bytes" % (relative, len(data)))
