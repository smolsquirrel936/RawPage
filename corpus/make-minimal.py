#!/usr/bin/env python3
"""Generates corpus/minimal.pdf -- one valid page, no compression, no fonts embedded.

WHY A GENERATOR AND NOT A COMMITTED-ONCE BLOB: a PDF cross-reference table stores ABSOLUTE
BYTE OFFSETS. Hand-editing the file by one character invalidates every offset after it, and
the result is a file that some readers repair silently and others reject -- which makes it
useless as a test fixture, because you no longer know which path you are testing.

This is Stage 0's single seed file, needed by the PDFium spike (task T12, risk A6). The real
corpus -- encrypted, signed, tagged, XFA, and the malformed set -- belongs to Stage 2.

    python corpus/make-minimal.py
"""

import pathlib

CONTENT = b"BT /F1 24 Tf 72 760 Td (Recto corpus seed) Tj ET\n"


def build() -> bytes:
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595.276 841.89] "
        b"/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        b"<< /Length " + str(len(CONTENT)).encode() + b" >>\nstream\n" + CONTENT + b"endstream",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    ]

    out = bytearray()
    # A binary comment on line 2 marks the file as binary for tools that sniff it -- and it
    # is why .gitattributes must treat corpus/** as binary: normalising these bytes would
    # shift every offset below.
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
    target = pathlib.Path(__file__).with_name("minimal.pdf")
    data = build()
    target.write_bytes(data)
    print(f"wrote {target} ({len(data)} bytes)")
