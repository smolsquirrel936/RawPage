#!/usr/bin/env python3
"""Does each Stage 5 text fixture carry the fault or feature its `PROVENANCE.md` row claims?

`Stage5-TextSearch-Testing-Guide.md` case A-14, run as `gates.corpus.faults` through
`make-corpus.py --check-faults`. The rule it exists for is Stage 3's A-17: *a fixture that does not
carry its fault tests nothing*. `--verify` proves a file regenerates identically; it cannot say
whether the file ever contained what the tests downstream of it assume -- a /ToUnicode that is
there, Hebrew emitted in VISUAL order, a control word that is absent. This reads the BYTES, with
every stream decoded, and never through `pdf/cos/` or PDFium: a check that used the reader under
test to judge the reader's fixtures would agree with itself.

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
    return problems
