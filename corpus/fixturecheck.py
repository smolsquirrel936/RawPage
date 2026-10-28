#!/usr/bin/env python3
"""Does each Stage 5 text fixture, and each Stage 6 annotation fixture, carry the fault or feature
its `PROVENANCE.md` row claims?

`Stage5-TextSearch-Testing-Guide.md` case A-14, run as `gates.corpus.faults` through
`make-corpus.py --check-faults`. The rule it exists for is Stage 3's A-17: *a fixture that does not
carry its fault tests nothing*. `--verify` proves a file regenerates identically; it cannot say
whether the file ever contained what the tests downstream of it assume -- a /ToUnicode that is
there, Hebrew emitted in VISUAL order, a control word that is absent. This reads the BYTES, with
every stream decoded, and never through `pdf/cos/` or PDFium: a check that used the reader under
test to judge the reader's fixtures would agree with itself.

Stage 6 T1 (`Stage6-Annotations-Testing-Guide.md` case A-15) adds `check_annots`: the annotation
fixtures, the FDF and XFDF exchange files and the stamp images, each held to its row -- and every
`labels/annots-*.json` entry held to the object it names, so a label cannot describe an annotation
the file does not carry. Object streams are expanded here too, because `annots-900.pdf` keeps its
9,000 dictionaries in one.

Decoding uses `zlib.decompress`. That is safe here although `corpus/deflate.py` exists precisely to
avoid `zlib.compress`: compression output varies between zlib builds, decompression of a valid
stream does not.
"""

import re
import zlib

def _streams(data):
    """(dictionary bytes, raw body) for every stream: from each `stream` keyword back to its
    `obj` header, forward to `endstream`. Not one regex over `<< >>`, because a stream dictionary
    may hold a dictionary of its own (an embedded file's /Params) and a lazy match stops inside it."""
    for match in re.finditer(rb"stream\r?\n", data):
        if data[max(0, match.start() - 3):match.start()] == b"end":
            continue
        end = data.find(b"\nendstream", match.end())
        if end < 0:
            continue
        header = data.rfind(b" obj", 0, match.start())
        yield data[max(0, header):match.start()], data[match.end():end]


def decoded(data, depth=0):
    """The file's bytes followed by every stream decoded -- and, for a stream that is itself a
    PDF (an attachment), that PDF's decoded bytes too, to the given depth."""
    parts = [data]
    for dictionary, body in _streams(data):
        if b"/FlateDecode" in dictionary:
            try:
                body = zlib.decompress(body)
            except zlib.error:
                continue
        parts.append(body)
        if body.startswith(b"%PDF-") and depth < 3:
            parts.append(decoded(body, depth + 1))
    return b"\n".join(parts)


def content_streams_with(data, marker):
    out = []
    for dictionary, body in _streams(data):
        if b"/FlateDecode" in dictionary:
            try:
                body = zlib.decompress(body)
            except zlib.error:
                continue
        if marker in body:
            out.append(body)
    return out


def utf16(text):
    """How `pdfbuild.utf16` writes a text string, so a check can look for it in the bytes."""
    import pdfbuild
    return pdfbuild.utf16(text)


def check(files):
    """{relative path: bytes} -> list of problems. Checks only the files it knows; the absence of
    a file it knows is itself a problem, so renaming a fixture cannot quietly skip its check."""
    problems = []

    def need(name):
        if name not in files:
            problems.append("FAULT     %s -- the fixture is missing" % name)
            return None
        return files[name]

    def claim(name, condition, what):
        if not condition:
            problems.append("FAULT     %s -- %s" % (name, what))

    import fontenc

    name = "ordinary/text-tounicode.pdf"
    data = need(name)
    if data is not None:
        blob = decoded(data)
        claim(name, blob.count(b"/ToUnicode") >= 3, "fewer than three fonts carry a /ToUnicode")
        claim(name, b"<80> <FB01>" in blob, "no code maps to U+FB01 (the ligature as one character)")
        claim(name, b"<81> <00660069>" in blob, "no code maps to 'fi' (the ligature as two letters)")
        claim(name, b"<82> <0301>" in blob, "no code maps to U+0301 (the decomposed acute)")
        claim(name, b"/Differences" in blob and b"/fi" in blob, "no /Differences naming /fi")

    name = "ordinary/cjk-embedded.pdf"
    data = need(name)
    if data is not None:
        claim(name, b"/ToUnicode" not in decoded(data),
              "it has a /ToUnicode -- it is the UNMAPPED control for text-tounicode.pdf page 2")

    name = "ordinary/rtl-text.pdf"
    data = need(name)
    if data is not None:
        _, mapping, _ = fontenc.hebrew_font()
        by_gid = {gid: chr(code) for code, gid in mapping.items()}
        pages = content_streams_with(data, b"/F3")
        claim(name, len(pages) == 2, "expected two pages drawing Hebrew, found %d" % len(pages))
        if len(pages) == 2:
            first_run = re.search(rb"/F3 [\d.]+ Tf [^<]*<([0-9A-F]+)> Tj", pages[0])
            text = ""
            if first_run:
                hexed = first_run.group(1).decode()
                text = "".join(by_gid.get(int(hexed[i:i + 4], 16), "?")
                               for i in range(0, len(hexed), 4))
            claim(name, text == "תיב םולש",
                  "page 1's first Hebrew run is not line 1 in VISUAL order (found %r)" % text)
            first_glyph = re.search(rb"/F3 [\d.]+ Tf [^<]*<([0-9A-F]{4})> Tj", pages[1])
            claim(name, first_glyph is not None
                  and by_gid.get(int(first_glyph.group(1), 16)) == "ש",
                  "page 2 does not begin with line 1's FIRST LOGICAL letter (shin)")

    name = "ordinary/cjk-vertical.pdf"
    data = need(name)
    if data is not None:
        blob = decoded(data)
        claim(name, b"/Identity-V" in blob and b"/Identity-H" in blob,
              "it lacks the vertical font or its horizontal control")
        claim(name, b"/ToUnicode" in blob, "the vertical font has no /ToUnicode")

    name = "ordinary/cjk-readable.pdf"
    data = need(name)
    if data is not None:
        blob = decoded(data)
        claim(name, b"/UniGB-UCS2-H" in blob and b"/UniGB-UCS2-V" in blob,
              "it lacks the predefined Unicode CMaps")
        claim(name, b"/FontFile" not in blob, "it embeds a font -- it exists to NOT embed one")

    name = "ordinary/hyphenated.pdf"
    data = need(name)
    if data is not None:
        blob = decoded(data)
        claim(name, b"(The renderer draws every page of the exam-)" in blob,
              "no line ends in a soft hyphen")
        claim(name, b"(and a well-known trick keeps the cache warm)" in blob,
              "the mid-line compound control is missing")

    name = "ordinary/search-classes.pdf"
    data = need(name)
    if data is not None:
        blob = decoded(data)
        claim(name, b"papaya" in blob, "the page-text word is missing")
        claim(name, utf16("quokka") in blob, "no annotation /Contents carries quokka")
        claim(name, utf16("zeppelin") in blob and b"/Outlines" in blob,
              "no outline title carries zeppelin")
        claim(name, utf16("marmot") in blob and utf16("walrus") in blob,
              "no field carries marmot as its name and walrus as its value")
        claim(name, b"/EmbeddedFiles" in blob and b"(ocelot.txt)" in blob
              and b"(lemur.pdf)" in blob, "the two attachments are not named")
        claim(name, utf16("narwhal") in blob, "the text attachment has no description narwhal")
        claim(name, b"axolotl" in blob, "the attached PDF's page text is missing")
        claim(name, b"pangolin" in blob, "the nested attachment -- the one-level control -- is missing")
        claim(name, b"wombat" not in blob.lower(),
              "the CONTROL word appears in the file, decoded streams included")

    name = "ordinary/text-900.pdf"
    data = need(name)
    if data is not None:
        blob = decoded(data)
        claim(name, b"/Count 900" in blob, "the page tree does not count 900 pages")
        claim(name, b"unique-page-0001 of 900" in blob and b"unique-page-0900 of 900" in blob,
              "the first or last page's marker is missing")

    authors = set()
    for fixture in ("columns-two", "columns-three", "table-ruled", "rtl-text", "cjk-vertical",
                    "hyphenated", "text-tounicode", "cjk-readable", "search-classes", "text-900"):
        data = files.get("ordinary/%s.pdf" % fixture)
        if data is None:
            continue
        blob = decoded(data)            # an Info dictionary may sit in an object stream
        found = re.search(rb"/Author (\([^)]*\))", blob)
        claim("ordinary/%s.pdf" % fixture, found is not None and b"/Subject" in blob,
              "no /Author or /Subject for Advanced Search's metadata filter (plan D5-23)")
        if found:
            authors.add(found.group(1))
    claim("ordinary/", len(authors) >= 4,
          "the Stage 5 fixtures name %d distinct authors; the metadata filter needs several"
          % len(authors))
    return problems + check_annots(files) + check_assembly(files)


# ---------------------------------------------------------------------------------------------
# Stage 6 T1 -- the annotation fixtures, the exchange files and the stamp images
# (Stage6-Annotations-Testing-Guide.md case A-15)
# ---------------------------------------------------------------------------------------------

def objects(data):
    """{object number: body} for every indirect object, those packed in object streams included --
    `annots-900.pdf` keeps its 9,000 dictionaries in one, and a check that read only the loose
    objects would find none of them."""
    found = {}
    for match in re.finditer(rb"(?m)^(\d+) 0 obj\n(.*?)\nendobj", data, re.S):
        found[int(match.group(1))] = match.group(2)
    for number, body in list(found.items()):
        if b"/Type /ObjStm" not in body.split(b"stream", 1)[0]:
            continue
        head = body.split(b"\nstream\n", 1)
        payload = zlib.decompress(head[1].rsplit(b"\nendstream", 1)[0])
        count = int(re.search(rb"/N (\d+)", head[0]).group(1))
        first = int(re.search(rb"/First (\d+)", head[0]).group(1))
        pairs = [int(v) for v in payload[:first].split()]
        offsets = [(pairs[i], pairs[i + 1]) for i in range(0, 2 * count, 2)]
        for index, (inner, offset) in enumerate(offsets):
            end = offsets[index + 1][1] if index + 1 < count else len(payload) - first
            found[inner] = payload[first + offset:first + end].strip()
    return found


def annotation_bodies(data):
    return {n: b for n, b in objects(data).items() if b"/Type /Annot " in b}


def _ref(body, key):
    found = re.search(rb"%s (\d+) 0 R" % re.escape(key), body)
    return int(found.group(1)) if found else None


def _flags(body):
    found = re.search(rb"/F (\d+)", body)
    return int(found.group(1)) if found else 0


def check_annots(files):
    import json
    import xml.etree.ElementTree as ElementTree

    problems = []

    def need(name):
        if name not in files:
            problems.append("FAULT     %s -- the fixture is missing" % name)
            return None
        return files[name]

    def claim(name, condition, what):
        if not condition:
            problems.append("FAULT     %s -- %s" % (name, what))

    def file_id(data):
        found = re.search(rb"/ID \[ <([0-9A-F]+)>", data)
        return found.group(1).decode() if found else None

    # Every label entry names an object that IS that annotation: its subtype, and its /NM where the
    # label gives one. The label is the generator's claim; this holds it to the bytes it describes.
    for relative, data in files.items():
        if not (relative.startswith("labels/annots-") and relative != "labels/annots-migration.json"):
            continue
        label = json.loads(data.decode("utf-8"))
        pdf = files.get(label["file"])
        if pdf is None:
            continue
        bodies = objects(pdf)
        entries = 0
        for page in label["pages"]:
            for entry in page["annots"]:
                entries += 1
                if entry["object"] is None:
                    continue
                body = bodies.get(entry["object"], b"")
                claim(label["file"], (b"/Subtype /%s" % entry["subtype"].encode()) in body,
                      "label says object %d is a %s, and it is not"
                      % (entry["object"], entry["subtype"]))
                if "nm" in entry:
                    claim(label["file"], (b"/NM (%s)" % entry["nm"].encode()) in body,
                          "object %d does not carry the label's /NM" % entry["object"])
        claim(relative, entries > 0, "the label lists no annotation")

    name = "ordinary/annots-every-type.pdf"
    data = need(name)
    if data is not None:
        annots = annotation_bodies(data)
        subtypes = {re.search(rb"/Subtype /(\w+)", b).group(1).decode() for b in annots.values()}
        wanted = {"Text", "Popup", "FreeText", "Line", "Square", "Circle", "Polygon", "PolyLine",
                  "Ink", "Highlight", "Underline", "StrikeOut", "Squiggly", "Caret", "Stamp",
                  "FileAttachment", "Sound", "Redact"}
        claim(name, wanted <= subtypes, "missing subtypes %s" % sorted(wanted - subtypes))
        claim(name, all(b"/AP " in b for b in annots.values() if b"/Subtype /Popup" not in b),
              "an annotation other than the pop-up has no /AP")
        blob = b"\n".join(annots.values())
        for marker in (b"/IT /FreeTextCallout", b"/IT /FreeTextTypeWriter", b"/BE << /S /C",
                       b"/RO ", b"/Sound ", b"/FS ", b"/LE [ /OpenArrow /ClosedArrow ]"):
            claim(name, marker in blob, "no annotation carries %s" % marker.decode())

    name = "ordinary/annots-no-ap.pdf"
    data = need(name)
    if data is not None:
        annots = annotation_bodies(data).values()
        bare = [b for b in annots if b"/Contents (no appearance" in b]
        drawn = [b for b in annots if b"/Contents (with appearance" in b]
        claim(name, len(bare) == 9 and not any(b"/AP" in b for b in bare),
              "page 1 must hold nine annotations and NONE with /AP (%d, %d with)"
              % (len(bare), sum(1 for b in bare if b"/AP" in b)))
        claim(name, len(drawn) == 9 and all(b"/AP " in b for b in drawn),
              "page 2 must hold the same nine, each WITH /AP")

    name = "ordinary/annots-threads.pdf"
    data = need(name)
    if data is not None:
        annots = annotation_bodies(data)
        blob = b"\n".join(annots.values())
        for marker in (b"/RT /Group", b"/StateModel (Review)", b"/StateModel (Marked)",
                       b"/State (Accepted)", b"/State (Rejected)", b"/State (Completed)",
                       b"/State (Cancelled)", b"/State (Unmarked)", b"/RC "):
            claim(name, marker in blob, "no annotation carries %s" % marker.decode())
        claim(name, b"/T <FEFF" in blob or b"/T (\\376\\377" in blob,
              "no author is written in UTF-16 (the non-ASCII author)")
        parent = {n: _ref(b, b"/IRT") for n, b in annots.items() if _ref(b, b"/IRT")}
        cycle = [n for n, p in parent.items() if parent.get(p) == n]
        claim(name, len(cycle) == 2, "no /IRT cycle of two")
        claim(name, any(p not in annots for p in parent.values()),
              "no reply whose parent object is missing")
        claim(name, any(p in annots and _ref(annots[p], b"/P") != _ref(annots[n], b"/P")
                        for n, p in parent.items()), "no reply on another page than its parent")
        nested = [n for n, p in parent.items() if p in parent and parent.get(p) != n]
        claim(name, nested, "no reply to a reply")

    name = "ordinary/annots-flags.pdf"
    data = need(name)
    if data is not None:
        flags = [_flags(b) for b in annotation_bodies(data).values()]
        for flag, bit in (("Locked", 128), ("LockedContents", 512), ("ReadOnly", 64),
                          ("Hidden", 2), ("NoView", 32), ("NoZoom", 8), ("NoRotate", 16)):
            claim(name, sum(1 for f in flags if f & bit) == 1,
                  "%s is not set on exactly one annotation" % flag)
        claim(name, sum(1 for f in flags if f == 4) == 7, "there are not seven unflagged twins")

    name = "ordinary/annots-rotated.pdf"
    data = need(name)
    if data is not None:
        pages = [b for b in objects(data).values() if re.search(rb"/Type /Page\b(?!s)", b)]
        rotations = sorted(int(m.group(1)) if m else 0 for m in
                           (re.search(rb"/Rotate (\d+)", b) for b in pages))
        claim(name, rotations == [0, 90, 180, 270], "page rotations are %s" % rotations)
        claim(name, b"/Matrix [ 0 1 -1 0 0 0 ]" in decoded(data), "no rotated appearance /Matrix")

    name = "ordinary/annots-direct.pdf"
    data = need(name)
    if data is not None:
        claim(name, re.search(rb"/Annots \[ <<", data) is not None,
              "no direct annotation dictionary in an /Annots array")
        listed = []
        for match in re.finditer(rb"/Annots \[", data):
            # Bracket-matched, because a direct dictionary inside the array has arrays of its own.
            depth, at = 1, match.end()
            while depth:
                depth += {ord("["): 1, ord("]"): -1}.get(data[at], 0)
                at += 1
            inside = re.sub(rb"<<.*?>>", b"", data[match.end():at - 1], flags=re.S)
            listed += set(re.findall(rb"(\d+) 0 R", inside))
        claim(name, any(listed.count(n) > 1 for n in listed),
              "no annotation is listed by two pages")
        bodies = annotation_bodies(data)
        claim(name, any(b"/P " not in b for b in bodies.values()),
              "no indirect annotation without /P")
        listed_numbers = {int(n) for n in listed}
        unlisted = [n for n, b in bodies.items() if n not in listed_numbers and b"/P " not in b]
        claim(name, len(unlisted) == 1, "not exactly one annotation that no page lists (%d)"
              % len(unlisted))
        claim(name, any(_ref(b, b"/IRT") in unlisted for b in bodies.values()),
              "nothing points at the unlisted annotation, so no reader could meet it")

    name = "ordinary/annots-unknown-keys.pdf"
    data = need(name)
    if data is not None:
        for marker in (b"/PieceInfo", b"/RawPageTestKey", b"/RC "):
            claim(name, marker in data, "%s is missing" % marker.decode())

    name = "ordinary/annots-900.pdf"
    data = need(name)
    if data is not None:
        claim(name, b"/Count 900" in decoded(data), "the page tree does not count 900 pages")
        count = len(annotation_bodies(data))
        claim(name, count == 9000, "it holds %d annotations, not 9,000" % count)

    name = "ordinary/attachments.pdf"
    data = need(name)
    if data is not None:
        claim(name, b"/EmbeddedFiles" in data and all(
            n in data for n in (b"(appendix.pdf)", b"(readme.txt)", b"(setup.exe)")),
            "the three document-level files are not all named")
        claim(name, data.count(b"/Desc ") == 4, "not every file carries a /Desc")
        claim(name, b"/Subtype /FileAttachment" in data, "no FileAttachment annotation")

    name = "ordinary/measure-scale.pdf"
    data = need(name)
    if data is not None:
        claim(name, data.count(b"/Type /Measure /Subtype /RL") == 3, "not three /Measure /RL")
        for intent in (b"/IT /LineDimension", b"/IT /PolyLineDimension", b"/IT /PolygonDimension"):
            claim(name, intent in data, "%s is missing" % intent.decode())

    name = "ordinary/annots-report-v1.pdf"
    data = need(name)
    original = files.get("compare/report-v1.pdf")
    if data is not None and original is not None:
        mine = set(content_streams_with(data, b" Tj"))
        theirs = set(content_streams_with(original, b" Tj"))
        claim(name, theirs and theirs <= mine,
              "its pages are not compare/report-v1.pdf's content streams")
        claim(name, len(annotation_bodies(data)) >= 8, "fewer than eight annotations")

    every_id = file_id(files.get("ordinary/annots-every-type.pdf", b""))
    threads_id = file_id(files.get("ordinary/annots-threads.pdf", b""))
    for kind, target_id, target in (("every-type", every_id, "annots-every-type.pdf"),
                                    ("threads", threads_id, "annots-threads.pdf")):
        name = "exchange/%s.fdf" % kind
        data = need(name)
        if data is not None:
            claim(name, data.startswith(b"%FDF-1.2\n"), "not an FDF header")
            claim(name, (b"/F (%s)" % target.encode()) in data, "/F does not name its target")
            claim(name, target_id is not None and ("<%s>" % target_id).encode() in data,
                  "its /ID is not its target's")
            claim(name, b"/Page 0" in data and b"/NM (" in data and b"/P " not in data,
                  "annotations are not in Acrobat's FDF shape (/Page, /NM, no /P)")
            claim(name, b"xref" not in data, "it carries a cross-reference table")
        name = "exchange/%s.xfdf" % kind
        data = need(name)
        if data is not None:
            try:
                root = ElementTree.fromstring(data)
            except ElementTree.ParseError as error:
                root = None
                claim(name, False, "does not parse: %s" % error)
            if root is not None:
                ns = "{http://ns.adobe.com/xfdf/}"
                elements = {child.tag.replace(ns, "") for child in root.find(ns + "annots")}
                claim(name, root.find(ns + "f").get("href") == target, "<f> does not name it")
                claim(name, root.find(ns + "ids").get("original") == target_id,
                      "<ids> is not its target's /ID")
                if kind == "every-type":
                    wanted = {"text", "freetext", "line", "square", "circle", "polygon",
                              "polyline", "ink", "highlight", "underline", "strikeout",
                              "squiggly", "caret", "stamp", "fileattachment", "sound", "redact"}
                    claim(name, wanted <= elements, "missing elements %s"
                          % sorted(wanted - elements))
                else:
                    for marker in (b"inreplyto=", b'replyType="group"', b'statemodel="Review"',
                                   b'statemodel="Marked"', b"<contents-richtext>"):
                        claim(name, marker in data, "%s is missing" % marker.decode())

    # S6 guide I-27 and I-28: the two places Acrobat 26.2.21931's exports were measured to differ from
    # `threads.*`, each file carrying its own and nothing of the standard's form beside it.
    name = "exchange/threads-acrobat26.xfdf"
    data = need(name)
    if data is not None:
        claim(name, b"<contents-richtext>" in data and b"<contents>" not in data,
              "its text is not in <contents-richtext> alone")
    name = "exchange/threads-acrobat26.fdf"
    data = need(name)
    if data is not None:
        claim(name, re.search(rb"/IRT \(", data) is not None
              and re.search(rb"/IRT \d+ 0 R", data) is None,
              "its /IRT is not the parent's /NM as a string")

    name = "exchange/malformed.xfdf"
    data = need(name)
    if data is not None:
        try:
            ElementTree.fromstring(data)
            claim(name, False, "it parses -- it exists not to")
        except ElementTree.ParseError:
            pass
    name = "exchange/external-entity.xfdf"
    data = need(name)
    if data is not None:
        claim(name, b"<!ENTITY rawpageprobe SYSTEM" in data and b"&rawpageprobe;" in data,
              "no external entity declared and referenced")

    name = "stamps/seal-rgba.png"
    data = need(name)
    if data is not None:
        claim(name, data.startswith(b"\x89PNG\r\n\x1a\n") and data[25] == 6,
              "not a PNG of colour type 6 (RGBA)")
        if data[25] == 6:
            width = int.from_bytes(data[16:20], "big")
            idat = data.index(b"IDAT")
            raw = zlib.decompress(data[idat + 4:idat + int.from_bytes(data[idat - 4:idat], "big")
                                       + 4])
            stride = 1 + width * 4
            alphas = {raw[row + 1 + x * 4 + 3] for row in range(0, len(raw), stride)
                      for x in range(width)}
            claim(name, 0 in alphas and 255 in alphas and len(alphas) >= 3,
                  "alpha does not take 0, 255 and a value between")
    name = "stamps/signature.jpg"
    data = need(name)
    if data is not None:
        claim(name, data.startswith(b"\xff\xd8") and b"\xff\xc0" in data
              and b"\xff\xc2" not in data, "not a baseline (SOF0) JPEG")
    return problems


# ---------------------------------------------------------------------------------------------
# Stage 7 T1 -- the assembly fixtures (Stage7-Assembly-Testing-Guide.md case A-15)
# ---------------------------------------------------------------------------------------------

_IDENTITY = re.compile(rb"(?<![A-Za-z0-9-])[A-Z][A-Z0-9]*-[0-9]{4}(?![A-Za-z0-9-])")


def _stream_data(body):
    head, _, rest = body.partition(b"\nstream\n")
    data = rest.rsplit(b"\nendstream", 1)[0]
    if b"/FlateDecode" in head:
        data = zlib.decompress(data)
    return data


def _refs_in(body, key):
    """The object numbers a key names: one reference, or every reference in an array."""
    found = re.search(rb"%s (\d+) 0 R" % re.escape(key), body)
    if found:
        return [int(found.group(1))]
    found = re.search(rb"%s \[([^\]]*)\]" % re.escape(key), body)
    return [int(n) for n in re.findall(rb"(\d+) 0 R", found.group(1))] if found else []


def _shown_strings(bodies, page_body):
    """Every string a page's own content streams show with Tj -- not its annotations', whose
    appearances are not page text."""
    shown = []
    for number in _refs_in(page_body, b"/Contents"):
        data = _stream_data(bodies.get(number, b""))
        shown += re.findall(rb"\(((?:[^()\\]|\\.)*)\) Tj", data)
    return shown


def _page_tree(bodies, data):
    """The catalog's object number and the page objects in tree order, from the trailer's /Root
    down /Kids -- never from the label, which is what is being checked."""
    root = int(re.findall(rb"/Root (\d+) 0 R", data)[-1])
    out = []

    def walk(number, depth):
        body = bodies.get(number, b"")
        if depth > 32:
            return
        if re.search(rb"/Type /Pages\b", body):
            for kid in _refs_in(body, b"/Kids"):
                walk(kid, depth + 1)
        elif re.search(rb"/Type /Page\b", body):
            out.append(number)
    walk(_refs_in(bodies[root], b"/Pages")[0], 0)
    return root, out


def _pdfnum(value):
    import pdfbuild
    return pdfbuild.num(value)


def check_assembly(files):
    """Every `labels/assembly-*.json` claim held to the bytes of the file it names: the page tree's
    order, the identity each page's content shows, each reference in the form its entry names and
    at the page whose identity it gives, and each fixture's feature. The engine half -- PDFium
    extracting the same identities -- is `render.assembly labels ...`."""
    import json
    import pdfbuild

    problems = []

    def claim(name, condition, what):
        if not condition:
            problems.append("FAULT     %s -- %s" % (name, what))

    labels = {relative: json.loads(data.decode("utf-8")) for relative, data in files.items()
              if relative.startswith("labels/assembly-")}
    claim("labels/", len(labels) == 21, "%d assembly labels, expected 21" % len(labels))

    for relative, label in sorted(labels.items()):
        name = label["file"]
        data = files.get(name)
        if data is None:
            claim(name, False, "the fixture is missing")
            continue
        bodies = objects(data)
        root, tree = _page_tree(bodies, data)
        catalog = bodies[root]
        pages = label["pages"]
        identity_of = {page["object"]: page["identity"] for page in pages}

        claim(name, tree == [page["object"] for page in pages],
              "the page tree's order %s is not the label's %s" % (tree, [p["object"] for p in pages]))
        for page in pages:
            body = bodies.get(page["object"], b"")
            drawn = page.get("drawnIdentity", page["identity"])
            tokens = [t.decode() for s in _shown_strings(bodies, body) for t in _IDENTITY.findall(s)]
            claim(name, tokens == ([drawn] if drawn else []),
                  "page %d shows %s, the label says %s" % (page["index"], tokens, drawn))

        def target(entry, where):
            if entry.get("targetPage") is None:
                return
            claim(name, identity_of.get(entry["targetPage"]) == entry["targetIdentity"],
                  "%s targets object %s, which is not %s" % (where, entry["targetPage"],
                                                              entry["targetIdentity"]))

        def outline(entries):
            for entry in entries:
                body = bodies.get(entry["object"], b"")
                claim(name, pdfbuild.utf16(entry["title"]) in body,
                      "outline object %d is not titled %r" % (entry["object"], entry["title"]))
                if entry.get("integerPage") is not None:
                    claim(name, (b"/Dest [ %d /Fit ]" % entry["integerPage"]) in body,
                          "outline object %d does not name page %d by integer"
                          % (entry["object"], entry["integerPage"]))
                elif entry["form"] == "dest":
                    claim(name, (b"/Dest [ %d 0 R" % entry["targetPage"]) in body,
                          "outline object %d has no /Dest to its page" % entry["object"])
                else:
                    claim(name, (b"/S /GoTo /D [ %d 0 R" % entry["targetPage"]) in body,
                          "outline object %d has no GoTo action to its page" % entry["object"])
                target(entry, "outline object %d" % entry["object"])
                outline(entry["children"])
        if "outline" in label:
            claim(name, b"/Outlines " in catalog, "the catalog has no /Outlines")
            outline(label["outline"])

        named = {}
        for entry in label.get("namedDestinations", []):
            named[entry["name"]] = entry["targetIdentity"]
            target(entry, "named destination %s" % entry["name"])
            if entry["form"] == "namesTree":
                shape = ((rb"\(%s\) \[ %d 0 R" if entry["entryForm"] == "array"
                          else rb"\(%s\) << /D \[ %d 0 R") % (entry["name"].encode(), entry["targetPage"]))
            else:
                shape = rb"/%s \[ %d 0 R" % (entry["name"].encode(), entry["targetPage"])
            claim(name, re.search(shape, data) is not None,
                  "named destination %s is not in its %s form" % (entry["name"], entry["form"]))

        for entry in label.get("links", []):
            body = bodies.get(entry["object"], b"")
            claim(name, b"/Subtype /Link" in body, "object %d is not a link" % entry["object"])
            page_object = pages[entry["page"]]["object"]
            claim(name, entry["object"] in _refs_in(bodies[page_object], b"/Annots"),
                  "link %d is not in its page's /Annots" % entry["object"])
            form = entry["form"]
            shapes = {"dest": b"/Dest [ %d 0 R" % (entry.get("targetPage") or 0),
                      "goto": b"/S /GoTo /D [ %d 0 R" % (entry.get("targetPage") or 0),
                      "destString": b"/Dest (%s)" % entry.get("name", "").encode(),
                      "destName": b"/Dest /%s" % entry.get("name", "").encode(),
                      "gotoNamed": b"/S /GoTo /D (%s)" % entry.get("name", "").encode(),
                      "goToR": b"/S /GoToR /F (%s) /D [ %d /Fit ]"
                               % (entry.get("file", "").encode(), entry.get("pageNumber", -1))}
            claim(name, shapes[form] in body, "link %d is not in its %s form" % (entry["object"], form))
            if form in ("destString", "destName", "gotoNamed"):
                claim(name, named.get(entry["name"]) == entry["targetIdentity"],
                      "link %d's name does not resolve to %s" % (entry["object"], entry["targetIdentity"]))
            elif form == "goToR":
                claim(name, pages[entry["pageNumber"]]["identity"] == entry["targetIdentity"],
                      "link %d's remote page number is not %s" % (entry["object"], entry["targetIdentity"]))
            elif entry.get("outsidePageTree"):
                claim(name, entry["targetPage"] not in tree
                      and re.search(rb"/Type /Page\b", bodies.get(entry["targetPage"], b"")) is not None,
                      "link %d's target is not a page object outside the tree" % entry["object"])
            else:
                target(entry, "link %d" % entry["object"])

        page_of = {page["identity"]: page["object"] for page in pages if page["identity"]}
        for entry in label.get("structure", []):
            claim(name, (b"/Pg %d 0 R" % page_of.get(entry["pageIdentity"], -1)) in bodies.get(entry["object"], b""),
                  "structure element %d has no /Pg to %s" % (entry["object"], entry["pageIdentity"]))
        for thread in label.get("threads", []):
            for bead in thread["beads"]:
                claim(name, (b"/P %d 0 R" % page_of.get(bead["pageIdentity"], -1)) in bodies.get(bead["object"], b""),
                      "bead %d is not on %s" % (bead["object"], bead["pageIdentity"]))
        if "threads" in label:
            claim(name, b"/Threads [" in catalog, "the catalog has no /Threads")
        if "openAction" in label:
            claim(name, (b"/OpenAction [ %d 0 R" % label["openAction"]["targetPage"]) in catalog,
                  "the catalog's /OpenAction does not name its page")
            target(label["openAction"], "/OpenAction")
        if "pageLabelRanges" in label:
            claim(name, b"/PageLabels" in catalog and b"/S /r" in catalog and b"/P (A-)" in catalog,
                  "the three /PageLabels ranges are not in the catalog")
        if "structure" in label:
            claim(name, b"/StructTreeRoot" in catalog, "the catalog has no /StructTreeRoot")

        stem = name.split("/")[-1]
        if stem == "refs-none.pdf":
            for key in (b"/Outlines", b"/Names", b"/Dests", b"/PageLabels", b"/StructTreeRoot",
                        b"/Threads", b"/OpenAction"):
                claim(name, key not in catalog, "the catalog carries %s" % key.decode())
            claim(name, b"/Annots" not in data, "a page carries /Annots")

        if stem == "inherit-resources.pdf":
            for page in pages:
                body = bodies[page["object"]]
                for key in (b"/Resources", b"/MediaBox", b"/CropBox"):
                    claim(name, key not in body, "page %d carries its own %s" % (page["index"], key.decode()))
                claim(name, (b"/Rotate" in body) == ("Rotate" in page["ownKeys"]),
                      "page %d's own /Rotate is not as labelled" % page["index"])
                walked, at = [], page["object"]
                while _ref(bodies[at], b"/Parent") is not None:
                    at = _ref(bodies[at], b"/Parent")
                    walked.append(at)
                claim(name, list(reversed(walked)) == page["path"],
                      "page %d's ancestors are %s, not %s" % (page["index"], walked[::-1], page["path"]))
            shared = label["shared"]
            body = bodies[shared["resources"]]
            claim(name, (b"%d 0 R" % shared["font"]) in body and (b"/Im1 %d 0 R" % shared["image"]) in body,
                  "the shared resources do not name the shared font and image")
            claim(name, max(len(p["path"]) for p in pages) == 4, "no page sits under three intermediate nodes")

        if stem == "annots-on-pages.pdf":
            for page in pages:
                listed = _refs_in(bodies[page["object"]], b"/Annots")
                claim(name, [a["object"] for a in page["annots"]] == listed,
                      "page %d's /Annots is not the label's" % page["index"])
                for annot in page["annots"]:
                    body = bodies.get(annot["object"], b"")
                    claim(name, (b"/Subtype /%s" % annot["subtype"].encode()) in body,
                          "object %d is not a %s" % (annot["object"], annot["subtype"]))
                    for key, field in ((b"/IRT", "irt"), (b"/Popup", "popup"), (b"/Parent", "parent")):
                        if field in annot:
                            claim(name, _ref(body, key) == annot[field],
                                  "object %d's %s is not %s" % (annot["object"], key.decode(), annot[field]))
                    if annot["role"] == "link":
                        target(annot, "link %d" % annot["object"])
                    if annot["role"] == "widget":
                        claim(name, (b"/T (%s)" % annot["field"].encode()) in body,
                              "widget %d is not the field %s" % (annot["object"], annot["field"]))
            claim(name, b"/AcroForm" in catalog, "the catalog has no /AcroForm")

        if stem.startswith("merge-"):
            claim(name, pdfbuild.utf16(label["title"]) in data, "the /Title is not %r" % label["title"])
            for key in (b"/AcroForm", b"/OCProperties", b"/Outlines"):
                claim(name, key in catalog, "the catalog has no %s" % key.decode())
            for field in label["fields"]:
                claim(name, (b"/T (%s)" % field["name"].encode()) in bodies.get(field["object"], b""),
                      "object %d is not the field %s" % (field["object"], field["name"]))
            for layer in label["layers"]:
                claim(name, pdfbuild.utf16(layer["name"]) in bodies.get(layer["object"], b""),
                      "object %d is not the layer %s" % (layer["object"], layer["name"]))
            for comment in label["comments"]:
                claim(name, b"/Subtype /Text" in bodies.get(comment["object"], b""),
                      "object %d is not a comment" % comment["object"])

        if stem == "layers.pdf":
            config = catalog.split(b"/D <<", 1)[1] if b"/D <<" in catalog else b""
            locked = _refs_in(config, b"/Locked")
            off = _refs_in(config, b"/OFF")
            radio = re.search(rb"/RBGroups \[ \[([^\]]*)\]", config)
            radio = [int(n) for n in re.findall(rb"(\d+) 0 R", radio.group(1))] if radio else []
            for group in label["groups"]:
                body = bodies.get(group["object"], b"")
                claim(name, b"/Type /OCG" in body and pdfbuild.utf16(group["name"]) in body,
                      "object %d is not the group %s" % (group["object"], group["name"]))
                claim(name, (group["object"] in locked) == group["locked"], "the lock of %s" % group["name"])
                claim(name, (group["object"] not in off) == group["defaultOn"], "the state of %s" % group["name"])
                claim(name, (group["object"] in radio) == (group["radioGroup"] is not None),
                      "the radio group of %s" % group["name"])
                if group["usage"]:
                    claim(name, (b"/PrintState /%s" % group["usage"]["print"].encode()) in body
                          and (b"/ViewState /%s" % group["usage"]["view"].encode()) in body,
                          "the usage of %s" % group["name"])
            for page in pages:
                body = bodies[page["object"]]
                for prop, group in page["properties"].items():
                    claim(name, (b"/%s %d 0 R" % (prop.encode(), group)) in body,
                          "page %d has no /Properties entry %s" % (page["index"], prop))
                streams = b"".join(_stream_data(bodies[n]) for n in _refs_in(body, b"/Contents"))
                claim(name, len(set(re.findall(rb"/OC /(\w+) BDC", streams))) >= 2,
                      "page %d draws content in fewer than two groups" % page["index"])
            claim(name, b"/AS [" in catalog, "no /AS usage application")

        if stem in ("tagged-headings.pdf", "untagged-headings.pdf"):
            claim(name, (b"/StructTreeRoot" in catalog) == label["tagged"], "/StructTreeRoot is not as labelled")
            for heading in label["headings"]:
                if heading["object"] is not None:
                    claim(name, (b"/S /H%d" % heading["level"]) in bodies.get(heading["object"], b""),
                          "element %d is not an H%d" % (heading["object"], heading["level"]))
            twin = files.get("assembly/" + ("untagged-headings.pdf" if label["tagged"] else "tagged-headings.pdf"))
            if twin is not None:
                theirs = objects(twin)
                _, their_tree = _page_tree(theirs, twin)
                for page, other in zip(pages, their_tree):
                    claim(name, _shown_strings(bodies, bodies[page["object"]])
                          == _shown_strings(theirs, theirs[other]),
                          "page %d's text differs from its twin's" % page["index"])

        if stem == "split-blanks.pdf":
            for page in pages:
                body = bodies[page["object"]]
                if page["kind"] == "absent":
                    claim(name, b"/Contents" not in body, "page %d has /Contents" % page["index"])
                elif page["kind"] == "empty":
                    stream = _stream_data(bodies[_refs_in(body, b"/Contents")[0]])
                    claim(name, stream.strip() == b"q Q", "page %d's stream paints something" % page["index"])
                elif page["kind"] == "white":
                    stream = _stream_data(bodies[_refs_in(body, b"/Contents")[0]])
                    claim(name, stream.startswith(b"1 g") and b"1 g BT /F2 48" in stream
                          and b"0 g" not in stream.split(b"1 g BT /F2 48")[0],
                          "page %d is not drawn in white" % page["index"])
            claim(name, label["partStarts"] == [p["index"] for p in pages if p["heading"]],
                  "the part starts are not the pages with a heading")

        if stem == "boxes.pdf":
            first = bodies[pages[0]["object"]]
            for key in (b"/CropBox", b"/BleedBox", b"/TrimBox", b"/ArtBox"):
                claim(name, key in first, "page 1 has no %s" % key.decode())
            claim(name, b"/CropBox [ -40 " in bodies[pages[1]["object"]], "page 2's CropBox is inside its MediaBox")
            x0, y0, x1, y1 = pages[2]["contentBox"]
            stream = _stream_data(bodies[_refs_in(bodies[pages[2]["object"]], b"/Contents")[0]])
            claim(name, (b"%s %s %s %s re f" % tuple(_pdfnum(v) for v in (x0, y0, x1 - x0, y1 - y0))) in stream,
                  "page 3's content box is not the filled rectangle")

        if stem == "decorated-elsewhere.pdf":
            blob = decoded(data)
            for kind in (b"Header", b"Watermark"):
                claim(name, (b"/Type /Pagination /Subtype /%s >> BDC" % kind) in blob,
                      "no /Artifact pagination %s" % kind.decode())
                claim(name, (b"/Private /%s" % kind) in blob, "no /PieceInfo for the %s" % kind.decode())
            claim(name, b"/RawPageDecoration" not in blob, "it carries a RawPage decoration")

        if stem.startswith("odd-count-"):
            claim(name, len(tree) == label["count"], "%d pages, not %d" % (len(tree), label["count"]))

    name = "assembly/merge-not-a-pdf.pdf"
    claim(name, name in files and not files[name].startswith(b"%PDF"), "it is a PDF, or it is missing")
    return problems
