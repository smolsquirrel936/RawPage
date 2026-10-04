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
    return problems + check_annots(files)


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
        for marker in (b"/RT /Group", b"/StateModel /Review", b"/StateModel /Marked",
                       b"/State /Accepted", b"/State /Rejected", b"/State /Completed",
                       b"/State /Cancelled", b"/State /Unmarked", b"/RC "):
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
        claim(name, any(b"/P " not in b for b in annotation_bodies(data).values()),
              "no indirect annotation without /P")

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
