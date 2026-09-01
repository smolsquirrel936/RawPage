#!/usr/bin/env python3
"""Deterministic PDF assembly -- the shared spine under every `corpus/make-*.py` generator.

WHY THIS FILE EXISTS AT ALL. `corpus/make-minimal.py` (Stage 0) and `corpus/make-a2-text-page.py`
(Stage 2 T3) each carry their own copy of the xref writer, which was right when there were two
files. T4 brings the count to roughly seventy across four directories, and seventy copies of an
offset table is seventy chances to write a file that is subtly wrong in a way no test can name.
The two Stage 0/T3 generators are deliberately NOT ported onto this module: their bytes are already
quoted in `Stage0-Toolchain-Testing-Guide.md` and in `Tech-Stack-Plan.md` section 3.3, and
"regenerates byte-identically" would stop being checkable for exactly the two files that have been
making that claim the longest.

WHAT DETERMINISM MEANS HERE, concretely. No clock, no `random`, no `id()`, no set iteration, no
`zlib` (see `deflate.py` for why), no dependence on the host's path separators or locale. Every
`/CreationDate` is a fixed instant, every /ID is derived from the file's own content, and every
mapping written out is written in insertion order. Run any generator twice, on any machine, and the
bytes are the same -- which is the property `gates.corpus.reproducible` asserts and the property the
whole all-generated-corpus decision (D2-11) rests on.

WHAT IT DOES NOT DO. It does not validate. A generator asking for a page tree whose /Count lies gets
a file whose /Count lies, on purpose -- that is what `corpus/malformed/` is made of. The distinction
between a valid file and a deliberately damaged one lives in the generator and in the
`corpus/PROVENANCE.md` row, never in this module.
"""

import hashlib

import deflate

# A fixed instant for every /CreationDate and /ModDate in the corpus. Real producers write the
# wall clock; a corpus that did would regenerate differently every second, which would make the
# reproducibility gate meaningless. 2026-08-31 is the date Stage 2's plan and guide were written.
FIXED_DATE = b"D:20260831000000Z"

PRODUCER = b"RawPage corpus generator"

# A4 and US Letter in points, the two page sizes the corpus uses for ordinary content.
A4 = (595.276, 841.89)
LETTER = (612.0, 792.0)
A5 = (419.528, 595.276)


# ---------------------------------------------------------------------------------------------
# Object syntax helpers. PDF dictionaries read best written literally, so these are small.
# ---------------------------------------------------------------------------------------------

def ref(number, generation=0):
    return b"%d %d R" % (number, generation)


def num(value):
    """A PDF number with no trailing zero noise. `%r` on a float is interpreter-stable in Python 3
    but produces things like 595.2760000000001 after arithmetic, so round explicitly."""
    if isinstance(value, int):
        return b"%d" % value
    text = "%.4f" % value
    text = text.rstrip("0").rstrip(".")
    return (text if text else "0").encode("ascii")


def name(text):
    """A PDF name, with #-escaping for anything outside the regular character set."""
    out = bytearray(b"/")
    for byte in text.encode("utf-8"):
        if 0x21 <= byte <= 0x7E and byte not in b"()<>[]{}/%#":
            out.append(byte)
        else:
            out += b"#%02X" % byte
    return bytes(out)


def literal(text):
    """A PDF literal string. Bytes in, bytes out -- callers that want PDFDocEncoding or UTF-16BE
    encode before calling, because guessing which one a caller meant is how mojibake ships."""
    if isinstance(text, str):
        text = text.encode("ascii")
    out = bytearray(b"(")
    for byte in text:
        if byte in b"()\\":
            out += b"\\" + bytes([byte])
        elif byte == 0x0D:
            out += b"\\r"
        elif byte < 0x20 or byte > 0x7E:
            out += b"\\%03o" % byte
        else:
            out.append(byte)
    out += b")"
    return bytes(out)


def hexstring(data):
    return b"<" + data.hex().upper().encode("ascii") + b">"


def utf16(text):
    """A text string in UTF-16BE with the byte-order mark PDF requires -- how a /Title or a
    bookmark label carries anything outside PDFDocEncoding, CJK included."""
    return literal(b"\xfe\xff" + text.encode("utf-16-be"))


def dictionary(pairs):
    """`pairs` is a list of (key_bytes, value_bytes). A list rather than a dict so the written
    order is the author's and is visible at the call site."""
    out = bytearray(b"<<")
    for key, value in pairs:
        out += b" " + key + b" " + value
    out += b" >>"
    return bytes(out)


def array(items):
    return b"[ " + b" ".join(items) + b" ]"


def rect(x0, y0, x1, y1):
    return array([num(x0), num(y0), num(x1), num(y1)])


# ---------------------------------------------------------------------------------------------
# The builder
# ---------------------------------------------------------------------------------------------

class Stream:
    """A stream object: its dictionary pairs and its already-encoded data.

    `data` is what lands between `stream` and `endstream`. Filters are applied by the caller
    through `flate()` / `ascii85()` / the codec modules, and the dictionary says so -- rather than
    the builder deciding, because half the point of several corpus files is WHICH filter chain
    they carry.
    """

    def __init__(self, pairs, data):
        self.pairs = list(pairs)
        self.data = data


class Pdf:
    """Accumulates numbered objects and serializes them with a real cross-reference table.

    Object numbers are handed out in allocation order starting at 1, and `reserve()` exists so a
    generator can name an object before it can build it -- a page's /Parent, an /Annots array
    pointing back at its page. A reserved-but-never-set object is an error at serialize time
    rather than a silently free entry, because a dangling reference in a file that claims to be
    well-formed is a mystery, and this corpus's malformed files are supposed to be the only
    mysterious ones.
    """

    def __init__(self, version="1.7"):
        self.version = version
        self._bodies = [None]          # index 0 is the free head; object N lives at index N
        self.encryptor = None          # callable(objnum, gen, data) -> bytes, set for encrypted files

    def reserve(self):
        self._bodies.append(None)
        return len(self._bodies) - 1

    def set(self, number, body):
        if number >= len(self._bodies):
            raise IndexError("object %d was never reserved" % number)
        self._bodies[number] = body
        return number

    def add(self, body):
        return self.set(self.reserve(), body)

    def add_stream(self, pairs, data):
        return self.add(Stream(pairs, data))

    def count(self):
        return len(self._bodies)

    # -- serialization -------------------------------------------------------------------------

    def _render(self, number, body):
        """One indirect object, complete with its `N 0 obj` / `endobj` envelope."""
        out = bytearray(b"%d 0 obj\n" % number)
        if isinstance(body, Stream):
            data = body.data
            if self.encryptor is not None:
                data = self.encryptor(number, 0, data)
            pairs = [(b"/Length", num(len(data)))] + body.pairs
            out += dictionary(pairs)
            out += b"\nstream\n"
            out += data
            out += b"\nendstream"
        else:
            out += body
        out += b"\nendobj\n"
        return bytes(out)

    def _file_id(self, payload):
        """/ID from a hash of the file's own body. Real producers seed it from the clock and the
        path; both would break reproducibility, and a content hash has the property /ID is
        actually for -- two different files get two different ids."""
        digest = hashlib.sha256(payload).digest()[:16]
        return array([hexstring(digest), hexstring(digest)])

    def serialize(self, root, info=None, extra_trailer=(), objstm=False, header_prefix=b""):
        """Assemble the file.

        `objstm=True` puts every eligible non-stream object into object streams and writes a
        cross-reference STREAM instead of a table -- the PDF 1.5 structure, which is what the
        900-page file and `ordinary/objstm-xrefstream.pdf` are for. Streams, and the catalog, stay
        as ordinary indirect objects because a stream cannot live inside an object stream and a
        reader that has not yet found the catalog cannot decompress one.

        `header_prefix` is prepended before `%PDF-`, which is how `malformed/junk-before-header.pdf`
        gets its damage without any special case in this method.
        """
        for number in range(1, len(self._bodies)):
            if self._bodies[number] is None:
                raise ValueError("object %d was reserved and never set" % number)

        if objstm:
            return self._serialize_xrefstream(root, info, extra_trailer, header_prefix)
        return self._serialize_classic(root, info, extra_trailer, header_prefix)

    def _preamble(self, header_prefix):
        out = bytearray(header_prefix)
        out += b"%%PDF-%s\n" % self.version.encode("ascii")
        # The binary comment on line 2. It is why `.gitattributes` must treat corpus/** as binary:
        # normalising these four bytes would shift every offset below them.
        out += b"%\xe2\xe3\xcf\xd3\n"
        return out

    def _serialize_classic(self, root, info, extra_trailer, header_prefix):
        out = self._preamble(header_prefix)
        offsets = [0] * len(self._bodies)
        for number in range(1, len(self._bodies)):
            offsets[number] = len(out)
            out += self._render(number, self._bodies[number])

        xref_offset = len(out)
        size = len(self._bodies)
        out += b"xref\n0 %d\n" % size
        out += b"0000000000 65535 f \n"
        for number in range(1, size):
            out += b"%010d 00000 n \n" % offsets[number]

        pairs = [(b"/Size", num(size)), (b"/Root", ref(root))]
        if info is not None:
            pairs.append((b"/Info", ref(info)))
        pairs.extend(extra_trailer)
        pairs.append((b"/ID", self._file_id(bytes(out))))
        out += b"trailer\n" + dictionary(pairs) + b"\n"
        out += b"startxref\n%d\n%%%%EOF\n" % xref_offset
        return bytes(out)

    def _serialize_xrefstream(self, root, info, extra_trailer, header_prefix):
        size = len(self._bodies) + 1          # +1 for the cross-reference stream itself
        xref_object = size - 1

        # Anything that is not a stream, and is not the catalog, goes into one object stream.
        # The catalog stays out: a reader locating /Root has not decompressed anything yet.
        packed = [n for n in range(1, len(self._bodies))
                  if not isinstance(self._bodies[n], Stream) and n != root]
        loose = [n for n in range(1, len(self._bodies)) if n not in set(packed)]

        objstm_number = None
        objstm_index = {}
        if packed:
            objstm_number = size
            size += 1
            header = bytearray()
            payload = bytearray()
            for position, number in enumerate(packed):
                objstm_index[number] = position
                header += b"%d %d " % (number, len(payload))
                payload += self._bodies[number] + b"\n"
            body = bytes(header) + bytes(payload)
            first = len(header)
            encoded = deflate.compress(body)
            objstm = Stream([(b"/Type", b"/ObjStm"), (b"/N", num(len(packed))),
                             (b"/First", num(first)), (b"/Filter", b"/FlateDecode")], encoded)
        else:
            objstm = None

        out = self._preamble(header_prefix)
        offsets = {}
        for number in loose:
            offsets[number] = len(out)
            out += self._render(number, self._bodies[number])
        if objstm is not None:
            offsets[objstm_number] = len(out)
            out += self._render(objstm_number, objstm)

        xref_offset = len(out)

        # /W [1 4 2]: one byte of type, four of offset-or-stream-number, two of generation-or-index.
        entries = bytearray()
        entries += bytes([0]) + (0).to_bytes(4, "big") + (0xFFFF).to_bytes(2, "big")
        for number in range(1, size):
            if number == xref_object:
                entries += bytes([1]) + xref_offset.to_bytes(4, "big") + (0).to_bytes(2, "big")
            elif number in offsets:
                entries += bytes([1]) + offsets[number].to_bytes(4, "big") + (0).to_bytes(2, "big")
            else:
                entries += (bytes([2]) + objstm_number.to_bytes(4, "big")
                            + objstm_index[number].to_bytes(2, "big"))

        pairs = [(b"/Type", b"/XRef"), (b"/Size", num(size)),
                 (b"/W", array([b"1", b"4", b"2"])), (b"/Root", ref(root))]
        if info is not None:
            pairs.append((b"/Info", ref(info)))
        pairs.extend(extra_trailer)
        pairs.append((b"/ID", self._file_id(bytes(out))))
        pairs.append((b"/Filter", b"/FlateDecode"))
        encoded = deflate.compress(bytes(entries))
        # Rendered by hand rather than through _render: the cross-reference stream is never
        # encrypted, even in an encrypted file (ISO 32000-1 section 7.5.8.2).
        body = bytearray(b"%d 0 obj\n" % xref_object)
        body += dictionary([(b"/Length", num(len(encoded)))] + pairs)
        body += b"\nstream\n" + encoded + b"\nendstream\nendobj\n"
        out += bytes(body)
        out += b"startxref\n%d\n%%%%EOF\n" % xref_offset
        return bytes(out)


def incremental_update(base, updates, root, info=None, size=None, extra_trailer=()):
    """Append one incremental revision to an existing file.

    `updates` is an ordered list of (object number, body) -- a body being either raw bytes or a
    `Stream`. The appended section carries only the changed objects and a `/Prev` pointing at the
    previous `startxref`, which is the structure Stage 3's incremental writer produces and the one
    `XrefTable` (T7) has to walk. Building it here, in the generator, is what lets
    `ordinary/incremental-3gen.pdf` exist before either of those does.

    The original bytes are never rewritten. That is the property the fixture is FOR: a reader that
    quietly rebuilds the file instead of following the /Prev chain still shows the right pages,
    and the only way to catch it is a file whose earlier revisions are still verbatim underneath.
    """
    previous_startxref = int(base.rsplit(b"startxref", 1)[1].split(b"%%EOF")[0].strip())
    out = bytearray(base)
    if not out.endswith(b"\n"):
        out += b"\n"

    renderer = Pdf()
    offsets = []
    for number, body in updates:
        offsets.append((number, len(out)))
        out += renderer._render(number, body)       # noqa: SLF001 -- same module, same author

    xref_offset = len(out)
    highest = size if size is not None else max(number for number, _ in updates) + 1
    out += b"xref\n"
    # One subsection per contiguous run of object numbers, which is what makes an incremental
    # xref small -- and is the part a reader most often gets wrong.
    run = []
    for number, offset in sorted(offsets):
        if run and number == run[-1][0] + 1:
            run.append((number, offset))
            continue
        if run:
            out += b"%d %d\n" % (run[0][0], len(run))
            for _, entry in run:
                out += b"%010d 00000 n \n" % entry
        run = [(number, offset)]
    if run:
        out += b"%d %d\n" % (run[0][0], len(run))
        for _, entry in run:
            out += b"%010d 00000 n \n" % entry

    pairs = [(b"/Size", num(highest)), (b"/Root", ref(root))]
    if info is not None:
        pairs.append((b"/Info", ref(info)))
    pairs.extend(extra_trailer)
    pairs.append((b"/Prev", num(previous_startxref)))
    pairs.append((b"/ID", Pdf()._file_id(bytes(out))))   # noqa: SLF001
    out += b"trailer\n" + dictionary(pairs) + b"\n"
    out += b"startxref\n%d\n%%%%EOF\n" % xref_offset
    return bytes(out)


# ---------------------------------------------------------------------------------------------
# Filter helpers
# ---------------------------------------------------------------------------------------------

def flate(data, mode="auto"):
    """(encoded bytes, filter name) for /FlateDecode."""
    return deflate.compress(data, mode), b"/FlateDecode"


def ascii85(data):
    """/ASCII85Decode, including the `~>` terminator PDF requires."""
    out = bytearray()
    for index in range(0, len(data), 4):
        chunk = data[index:index + 4]
        pad = 4 - len(chunk)
        chunk = chunk + b"\x00" * pad
        value = int.from_bytes(chunk, "big")
        if value == 0 and pad == 0:
            out += b"z"
            continue
        digits = bytearray()
        for _ in range(5):
            digits.append(33 + value % 85)
            value //= 85
        digits.reverse()
        out += bytes(digits[:5 - pad])
    # Wrap at 72 columns. Real producers wrap; a decoder that mishandles the newline inside an
    # ASCII85 stream is a real bug and a single-line fixture would never find it.
    wrapped = bytearray()
    for index in range(0, len(out), 72):
        wrapped += out[index:index + 72] + b"\n"
    return bytes(wrapped) + b"~>", b"/ASCII85Decode"


def asciihex(data):
    body = data.hex().upper().encode("ascii")
    wrapped = bytearray()
    for index in range(0, len(body), 72):
        wrapped += body[index:index + 72] + b"\n"
    return bytes(wrapped) + b">", b"/ASCIIHexDecode"


def runlength(data):
    """/RunLengthDecode, PDF's byte-oriented RLE. Literal runs carry length-1; repeats carry
    257-length; 128 ends the stream."""
    out = bytearray()
    index = 0
    size = len(data)
    while index < size:
        run = 1
        while index + run < size and run < 127 and data[index + run] == data[index]:
            run += 1
        if run >= 3:
            out.append(257 - run)
            out.append(data[index])
            index += run
            continue
        start = index
        while index < size:
            ahead = 1
            while index + ahead < size and ahead < 3 and data[index + ahead] == data[index]:
                ahead += 1
            if ahead >= 3:
                break
            index += 1
            if index - start == 128:
                break
        chunk = data[start:index]
        out.append(len(chunk) - 1)
        out += chunk
    out.append(128)
    return bytes(out), b"/RunLengthDecode"


def lzw(data, early_change=1):
    """/LZWDecode, PDF's variant of TIFF LZW: codes 256 = clear, 257 = EOD, variable width 9..12,
    and the notorious `EarlyChange` off-by-one that defaults to 1.

    Written here rather than skipped because `pdf/cos/filters/Lzw` (T7) has to read it and there
    is no LZW encoder in the standard library.
    """
    out = bytearray()
    acc = 0
    nbits = 0

    def put(code, width):
        nonlocal acc, nbits
        acc = (acc << width) | code
        nbits += width
        while nbits >= 8:
            out.append((acc >> (nbits - 8)) & 0xFF)
            nbits -= 8

    table = {bytes([byte]): byte for byte in range(256)}
    next_code = 258
    width = 9
    put(256, width)
    current = b""
    for byte in data:
        candidate = current + bytes([byte])
        if candidate in table:
            current = candidate
            continue
        put(table[current], width)
        table[candidate] = next_code
        next_code += 1
        if next_code + early_change > (1 << width) and width < 12:
            width += 1
        elif next_code + early_change > (1 << 12):
            put(256, width)
            table = {bytes([value]): value for value in range(256)}
            next_code = 258
            width = 9
        current = bytes([byte])
    if current:
        put(table[current], width)
    put(257, width)
    if nbits:
        out.append((acc << (8 - nbits)) & 0xFF)
    return bytes(out), b"/LZWDecode"


def png_predict(data, columns, colors, bpc=8, predictor=12):
    """Apply a PNG row predictor, the form /DecodeParms most often takes on an image.

    predictor 12 is PNG Up, chosen because it is the one whose output visibly differs from the
    input for a gradient and whose decode is a running sum -- so a decoder that ignores the
    predictor produces a wrong image rather than an accidentally right one.
    """
    if predictor != 12:
        raise ValueError("only PNG Up (12) is implemented; nothing in the corpus needs another")
    stride = (columns * colors * bpc + 7) // 8
    out = bytearray()
    previous = bytes(stride)
    for index in range(0, len(data), stride):
        row = data[index:index + stride]
        out.append(2)  # PNG filter type: Up
        out += bytes((row[i] - previous[i]) & 0xFF for i in range(len(row)))
        previous = row
    return bytes(out)


# ---------------------------------------------------------------------------------------------
# Page-tree helpers
# ---------------------------------------------------------------------------------------------

def info_object(pdf, title, subject=None):
    pairs = [(b"/Title", utf16(title)),
             (b"/Producer", literal(PRODUCER)),
             (b"/CreationDate", literal(FIXED_DATE)),
             (b"/ModDate", literal(FIXED_DATE))]
    if subject is not None:
        pairs.append((b"/Subject", utf16(subject)))
    return pdf.add(dictionary(pairs))


def content_stream(pdf, text, compressed=True, mode="auto"):
    """A page content stream. Uncompressed by default would be nicer to read, but most of the
    corpus wants /FlateDecode because that is what a real file has -- `ordinary/text-a4.pdf` keeps
    one uncompressed page so there is a fixture that can be read in a hex editor."""
    if compressed:
        data, filter_name = flate(text, mode)
        return pdf.add_stream([(b"/Filter", filter_name)], data)
    return pdf.add_stream([], text)


def base14(pdf, base_font="Helvetica"):
    return pdf.add(dictionary([(b"/Type", b"/Font"), (b"/Subtype", b"/Type1"),
                               (b"/BaseFont", name(base_font)),
                               (b"/Encoding", b"/WinAnsiEncoding")]))


def simple_document(pdf, pages, root_extra=(), pages_extra=()):
    """Build /Catalog and /Pages over already-built page objects.

    `pages` is a list of (page_number, page_pairs_builder) where the builder is called with the
    /Pages object number so a page can name its /Parent. Returns the catalog's object number.
    """
    pages_number = pdf.reserve()
    page_numbers = []
    for builder in pages:
        page_numbers.append(pdf.add(builder(pages_number)))
    pdf.set(pages_number, dictionary(
        [(b"/Type", b"/Pages"),
         (b"/Kids", array([ref(number) for number in page_numbers])),
         (b"/Count", num(len(page_numbers)))] + list(pages_extra)))
    catalog = pdf.add(dictionary(
        [(b"/Type", b"/Catalog"), (b"/Pages", ref(pages_number))] + list(root_extra)))
    return catalog, pages_number, page_numbers


def page(parent, media, contents, resources=b"<< >>", extra=()):
    pairs = [(b"/Type", b"/Page"), (b"/Parent", ref(parent)),
             (b"/MediaBox", rect(0, 0, media[0], media[1])),
             (b"/Resources", resources)]
    if contents is not None:
        pairs.append((b"/Contents", ref(contents)))
    pairs.extend(extra)
    return dictionary(pairs)


# ---------------------------------------------------------------------------------------------
# Deterministic synthetic prose -- shared so several files can be "text-heavy" the same way
# ---------------------------------------------------------------------------------------------

WORDS = [
    "document", "render", "raster", "glyph", "outline", "baseline", "kerning", "typeface",
    "page", "tile", "viewport", "cache", "revision", "scope", "invalidate", "schedule",
    "worker", "thread", "latency", "throughput", "percentile", "budget", "frame", "monitor",
    "resolution", "physical", "logical", "scaling", "policy", "boundary", "interface", "facade",
    "object", "stream", "filter", "table", "trailer", "offset", "repair", "report",
    "incremental", "append", "signature", "permission", "annotation", "layer", "tag", "journal",
]


def words_for(seed, count):
    """A fixed linear-congruential walk over WORDS -- the same construction
    `corpus/make-a2-text-page.py` uses, and for the same reason: `random`'s stream is an
    implementation detail of the interpreter, and a lorem-ipsum blob would make the line lengths
    whatever the paste happened to be. The constants are Numerical Recipes'."""
    out = []
    state = seed & 0xFFFFFFFF
    for _ in range(count):
        state = (1664525 * state + 1013904223) % (2 ** 32)
        out.append(WORDS[state % len(WORDS)])
    return out


def text_body(seed, lines, words_per_line=13):
    return [" ".join(words_for(seed * 1000 + index, words_per_line)) for index in range(lines)]


def show_text(x, y, size, text, font=b"/F1"):
    """One BT/ET text run. `text` is ASCII; anything else goes through a CID font and its own
    encoder, because a literal string in a simple font is bytes-as-glyph-codes and pretending
    otherwise is how a corpus file ends up rendering boxes."""
    return b"BT %s %s Tf 1 0 0 1 %s %s Tm %s Tj ET" % (
        font, num(size), num(x), num(y), literal(text))
