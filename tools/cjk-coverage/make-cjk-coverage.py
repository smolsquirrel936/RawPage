#!/usr/bin/env python3
"""tools/cjk-coverage/make-cjk-coverage.py -- writes core/doc/CjkCoverage.inc, the characters each of the
four predefined Adobe CJK collections can show in a typed comment. Stage 6 T7, decisions D6-7 and D6-34
(`claude-plan/Stage6-Annotations-Plan.md` section 3).

WHY A TABLE AT ALL. A typed comment's appearance draws CJK text in one of four NON-embedded CID fonts
through a predefined UCS-2 CMap (D6-7). The code written IS the character, so every BMP character can be
WRITTEN -- and a character the collection has no CID for is drawn as nothing, or as a box, in every reader.
D6-7 refuses such a character at input rather than draw it wrong, and refusing needs to know which
characters each collection holds. That is this table.

WHERE IT COMES FROM, AND WHAT CHECKS IT. Each collection's legacy character set, as Python's own CJK codecs
hold it -- the character sets the four collections were built on:

    Adobe-GB1   (STSong-Light,       UniGB-UCS2-H)   GBK      -- Adobe-GB1-2 is GBK
    Adobe-CNS1  (MSung-Light,        UniCNS-UCS2-H)  Big5     -- Adobe-CNS1-0 is Big5
    Adobe-Japan1(KozMinPro-Regular,  UniJIS-UCS2-H)  cp932    -- JIS X 0208 with the NEC and IBM rows
    Adobe-Korea1(HYSMyeongJo-Medium, UniKS-UCS2-H)   cp949    -- KS X 1001 and all 11,172 Hangul syllables

**A codec is a proxy, not the oracle.** `tests/render/tst_typedtext.cpp` writes EVERY character this table
claims, in its collection's font, through RawPage's own builder, opens the file in PDFium and requires the
engine to read each one back as written and to draw ink for it (guide case A-56). A claim the engine does
not honour fails that case, and the table is narrowed here -- never in the C++ -- in EXCLUDED, below. The
first measurement (2026-10-07) read back all 59,270 characters as written and found one that draws nothing.

ONLY CJK CHARACTERS, never a script the collections happen to carry. GB2312 and JIS X 0208 both hold Greek
and Cyrillic; D6-7 refuses those (`DW-3`), so the table is restricted to the blocks below: CJK ideographs,
kana, Hangul, Bopomofo, CJK punctuation and symbols, full-width forms, and the general symbol blocks
(arrows, mathematical operators, enclosed numbers, shapes) a CJK text uses. A character WinAnsiEncoding
holds is drawn in Helvetica and left out. Half-width forms are left out too: every CJK glyph is drawn
1,000 units wide (`/DW 1000`), and a half-width one would not be.

USAGE
    python tools/cjk-coverage/make-cjk-coverage.py            # writes core/doc/CjkCoverage.inc
    python tools/cjk-coverage/make-cjk-coverage.py --verify   # it regenerates byte-identically
    python tools/cjk-coverage/make-cjk-coverage.py --self-test  # the control: a changed bit is found

The output is LF-only and ASCII, whatever platform writes it.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "core" / "doc" / "CjkCoverage.inc"

# (the C++ enumerator, the codec)
COLLECTIONS = [
    ("Simplified", "gbk"),
    ("Traditional", "big5"),
    ("Japanese", "cp932"),
    ("Korean", "cp949"),
]

# The blocks a CJK font may draw for a typed comment -- see the module comment.
BLOCKS = [
    (0x2010, 0x2BFF),   # general punctuation .. miscellaneous symbols and arrows
    (0x2E80, 0x2FDF),   # CJK radicals, Kangxi radicals
    (0x3000, 0x33FF),   # CJK symbols and punctuation, kana, Bopomofo, Hangul jamo, enclosed CJK, CJK compatibility
    (0x3400, 0x4DBF),   # CJK unified ideographs extension A
    (0x4E00, 0x9FFF),   # CJK unified ideographs
    (0xAC00, 0xD7A3),   # Hangul syllables
    (0xF900, 0xFAFF),   # CJK compatibility ideographs
    (0xFE10, 0xFE1F),   # vertical forms
    (0xFE30, 0xFE6F),   # CJK compatibility forms, small form variants
    (0xFF01, 0xFF60),   # full-width forms (the half-width ones are left out)
    (0xFFE0, 0xFFE6),   # full-width signs
]


# CHARACTERS THE ENGINE MEASUREMENT REMOVED (`tests/render/tst_typedtext.cpp`, guide A-56): each is in its
# collection's character set and read back by PDFium as written, and draws NO INK in the face the product's
# font substitution chooses on the machine that measured it -- so a typed comment would show nothing there.
# Each row says when and on what it was measured; the table is narrowed here, never in the C++.
EXCLUDED = {
    # 2026-10-07, Stage 6 T7, Windows 11 26200, PMingLiU: U+223C TILDE OPERATOR, Big5 0xA1E3.
    "Traditional": {0x223C},
}


def winansi():
    """The characters WinAnsiEncoding holds -- Windows code page 1252's 251, which ISO 32000-1 Annex D's
    table follows code for code."""
    out = set()
    for byte in range(0x20, 0x100):
        try:
            out.add(ord(bytes([byte]).decode("cp1252")))
        except UnicodeDecodeError:
            pass
    return out


def coverage(name, codec):
    """The set of code points `codec` encodes, inside BLOCKS, outside WinAnsi and not EXCLUDED for `name`."""
    skip = winansi() | EXCLUDED.get(name, set())
    out = set()
    for low, high in BLOCKS:
        for code in range(low, high + 1):
            if code in skip:
                continue
            try:
                chr(code).encode(codec)
            except UnicodeEncodeError:
                continue
            out.add(code)
    return out


def pages(points):
    """{page: 256-bit int}, one bit per code point of the page."""
    out = {}
    for code in points:
        out[code >> 8] = out.get(code >> 8, 0) | (1 << (code & 0xFF))
    return out


def render(tables):
    lines = [
        "// core/doc/CjkCoverage.inc -- GENERATED by tools/cjk-coverage/make-cjk-coverage.py. Do not edit:",
        "// change the generator and run it. `gates.cjk_coverage.reproducible` holds this file to it, and",
        "// `tests/render/tst_typedtext.cpp` holds every character it claims to the engine (guide case A-56).",
        "//",
        "// One row per 256-code-point page a collection covers any of: the page, then its 256 bits as 64",
        "// hexadecimal digits, the LOWEST code point's bit last. Included by core/doc/TypedText.cpp only.",
        "",
    ]
    for name, _codec in COLLECTIONS:
        table = tables[name]
        count = sum(bin(bits).count("1") for bits in table.values())
        lines.append("// %s: %d characters on %d pages." % (name, count, len(table)))
        lines.append("static const CoveragePage k%sCoverage[] = {" % name)
        for page in sorted(table):
            lines.append('    {0x%02X, "%064X"},' % (page, table[page]))
        lines.append("};")
        lines.append("")
    return ("\n".join(lines)).encode("ascii")


def parse(data):
    """The rendered table read back, {collection: set of code points}, by the rule the C++ reader follows."""
    out = {}
    current = None
    for line in data.decode("ascii").split("\n"):
        found = re.match(r"static const CoveragePage k(\w+)Coverage\[\]", line)
        if found:
            current = out.setdefault(found.group(1), set())
            continue
        row = re.match(r'\s+\{0x([0-9A-F]{2}), "([0-9A-F]{64})"\},', line)
        if row and current is not None:
            page, bits = int(row.group(1), 16), int(row.group(2), 16)
            current.update((page << 8) | bit for bit in range(256) if bits >> bit & 1)
    return out


def build():
    return render({name: pages(coverage(name, codec)) for name, codec in COLLECTIONS})


def main(argv):
    data = build()
    if "--verify" in argv:
        current = OUTPUT.read_bytes() if OUTPUT.exists() else b""
        if current != data:
            print("core/doc/CjkCoverage.inc does NOT regenerate byte-identically -- run the generator")
            return 1
        print("core/doc/CjkCoverage.inc: 4 collections regenerate byte-identically")
        return 0
    if "--self-test" in argv:
        # THE TABLE READ BACK: the rendered rows parsed into code points must be the sets the codecs
        # gave -- which catches a bit written in the wrong order, the defect a byte comparison of the
        # file with its own regeneration can never see. THE CONTROL (convention 7): the same read of a
        # table with one bit changed must differ, or the read-back proves nothing.
        expected = {name: coverage(name, codec) for name, codec in COLLECTIONS}
        if parse(data) != expected:
            print("the rendered table does not read back as the codecs' sets")
            return 1
        changed = bytearray(data)
        at = data.index(b'"', data.index(b"kTraditionalCoverage")) + 1
        changed[at] = ord("1") if changed[at] == ord("0") else ord("0")
        if parse(bytes(changed)) == expected:
            print("cjk coverage self-test: the CONTROL did not fire -- a changed bit read back the same")
            return 1
        # And the rule the table rests on: Greek, which GBK and cp932 both hold, is NOT in it.
        if any(0x0391 <= code <= 0x03C9 for points in expected.values() for code in points):
            print("the coverage holds Greek -- the block filter did not apply")
            return 1
        print("cjk coverage self-test: the table reads back as the codecs' sets; 1 of 1 control fired; "
              "Greek refused")
        return 0
    OUTPUT.write_bytes(data)
    print("wrote %s (%d bytes)" % (OUTPUT.relative_to(ROOT).as_posix(), len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
