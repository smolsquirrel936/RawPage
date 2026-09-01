#!/usr/bin/env python3
"""CCITT Group 3 (T.4) and Group 4 (T.6) encoders, for the corpus's /CCITTFaxDecode streams.

WHAT THIS IS FOR. `pdf/cos/filters/Ccitt` (Stage 2 T7) has to read what a scanner produces, and a
scanned page is the single most common thing a PDF reader is handed that is not text. D2-11 says
the corpus contains CCITT image streams and that we generate them, so they are encoded here.

THE TWO CODINGS, and why both are present rather than just G4:

  * K = 0  -- Group 3 one-dimensional (Modified Huffman). Each row is coded independently as
             alternating white and black runs. No reference line, so a corrupt row cannot poison
             the rest of the image.
  * K < 0  -- Group 4 (Modified Modified READ, T.6). Each row is coded RELATIVE TO THE ROW ABOVE,
             in pass / horizontal / vertical modes. This is what real fax and scanner output uses,
             and its decoder is a different piece of code from the G3 one -- a corpus with only
             one of them leaves the other untested.

DETERMINISM. Integer arithmetic and fixed tables throughout. There is no arithmetic coder and no
floating point anywhere in this module.

SELF-CHECK. `encode()` decodes its own output with `decode()` -- an independent implementation in
the same file, written from the decoder's side of the tables rather than by inverting the encoder's
control flow -- and raises if the bitmap does not come back exactly. CCITT is lossless, so "exactly"
is the right word: unlike the JPEG check there is no tolerance to argue about. Without this the
corpus could ship an image only our own decoder agrees with, which would make the filter suite
agree with itself (CLAUDE.md convention 7).
"""

# T.4 Table 1/2: terminating codes for runs 0..63, as (bit count, code value). White then black.
_WHITE_TERM = [
    (8, 0x35), (6, 0x07), (4, 0x07), (4, 0x08), (4, 0x0B), (4, 0x0C), (4, 0x0E), (4, 0x0F),
    (5, 0x13), (5, 0x14), (5, 0x07), (5, 0x08), (6, 0x08), (6, 0x03), (6, 0x34), (6, 0x35),
    (6, 0x2A), (6, 0x2B), (7, 0x27), (7, 0x0C), (7, 0x08), (7, 0x17), (7, 0x03), (7, 0x04),
    (7, 0x28), (7, 0x2B), (7, 0x13), (7, 0x24), (7, 0x18), (8, 0x02), (8, 0x03), (8, 0x1A),
    (8, 0x1B), (8, 0x12), (8, 0x13), (8, 0x14), (8, 0x15), (8, 0x16), (8, 0x17), (8, 0x28),
    (8, 0x29), (8, 0x2A), (8, 0x2B), (8, 0x2C), (8, 0x2D), (8, 0x04), (8, 0x05), (8, 0x0A),
    (8, 0x0B), (8, 0x52), (8, 0x53), (8, 0x54), (8, 0x55), (8, 0x24), (8, 0x25), (8, 0x58),
    (8, 0x59), (8, 0x5A), (8, 0x5B), (8, 0x4A), (8, 0x4B), (8, 0x32), (8, 0x33), (8, 0x34),
]

_WHITE_MAKEUP = {
    64: (5, 0x1B), 128: (5, 0x12), 192: (6, 0x17), 256: (7, 0x37), 320: (8, 0x36),
    384: (8, 0x37), 448: (8, 0x64), 512: (8, 0x65), 576: (8, 0x68), 640: (8, 0x67),
    704: (9, 0xCC), 768: (9, 0xCD), 832: (9, 0xD2), 896: (9, 0xD3), 960: (9, 0xD4),
    1024: (9, 0xD5), 1088: (9, 0xD6), 1152: (9, 0xD7), 1216: (9, 0xD8), 1280: (9, 0xD9),
    1344: (9, 0xDA), 1408: (9, 0xDB), 1472: (9, 0x98), 1536: (9, 0x99), 1600: (9, 0x9A),
    1664: (6, 0x18), 1728: (9, 0x9B),
}

_BLACK_TERM = [
    (10, 0x37), (3, 0x02), (2, 0x03), (2, 0x02), (3, 0x03), (4, 0x03), (4, 0x02), (5, 0x03),
    (6, 0x05), (6, 0x04), (7, 0x04), (7, 0x05), (7, 0x07), (8, 0x04), (8, 0x07), (9, 0x18),
    (10, 0x17), (10, 0x18), (10, 0x08), (11, 0x67), (11, 0x68), (11, 0x6C), (11, 0x37),
    (11, 0x28), (11, 0x17), (11, 0x18), (12, 0xCA), (12, 0xCB), (12, 0xCC), (12, 0xCD),
    (12, 0x68), (12, 0x69), (12, 0x6A), (12, 0x6B), (12, 0xD2), (12, 0xD3), (12, 0xD4),
    (12, 0xD5), (12, 0xD6), (12, 0xD7), (12, 0x6C), (12, 0x6D), (12, 0xDA), (12, 0xDB),
    (12, 0x54), (12, 0x55), (12, 0x56), (12, 0x57), (12, 0x64), (12, 0x65), (12, 0x52),
    (12, 0x53), (12, 0x24), (12, 0x37), (12, 0x38), (12, 0x27), (12, 0x28), (12, 0x58),
    (12, 0x59), (12, 0x2B), (12, 0x2C), (12, 0x5A), (12, 0x66), (12, 0x67),
]

_BLACK_MAKEUP = {
    64: (10, 0x0F), 128: (12, 0xC8), 192: (12, 0xC9), 256: (12, 0x5B), 320: (12, 0x33),
    384: (12, 0x34), 448: (12, 0x35), 512: (13, 0x6C), 576: (13, 0x6D), 640: (13, 0x4A),
    704: (13, 0x4B), 768: (13, 0x4C), 832: (13, 0x4D), 896: (13, 0x72), 960: (13, 0x73),
    1024: (13, 0x74), 1088: (13, 0x75), 1152: (13, 0x76), 1216: (13, 0x77), 1280: (13, 0x52),
    1344: (13, 0x53), 1408: (13, 0x54), 1472: (13, 0x55), 1536: (13, 0x5A), 1600: (13, 0x5B),
    1664: (13, 0x64), 1728: (13, 0x65),
}

# Extended make-up codes, shared by both colours (T.4 Table 3).
_EXT_MAKEUP = {
    1792: (11, 0x08), 1856: (11, 0x0C), 1920: (11, 0x0D), 1984: (12, 0x12), 2048: (12, 0x13),
    2112: (12, 0x14), 2176: (12, 0x15), 2240: (12, 0x16), 2304: (12, 0x17), 2368: (12, 0x1C),
    2432: (12, 0x1D), 2496: (12, 0x1E), 2560: (12, 0x1F),
}

# T.6 Table 4: the two-dimensional mode codes.
_MODE_PASS = (4, 0x1)            # 0001
_MODE_HORIZONTAL = (3, 0x1)      # 001
_MODE_V0 = (1, 0x1)              # 1
_MODE_VR1 = (3, 0x3)             # 011
_MODE_VR2 = (6, 0x3)             # 000011
_MODE_VR3 = (7, 0x3)             # 0000011
_MODE_VL1 = (3, 0x2)             # 010
_MODE_VL2 = (6, 0x2)             # 000010
_MODE_VL3 = (7, 0x2)             # 0000010


class _BitWriter:
    def __init__(self):
        self.out = bytearray()
        self._acc = 0
        self._nbits = 0

    def put(self, pair):
        length, value = pair
        for shift in range(length - 1, -1, -1):
            self._acc = (self._acc << 1) | ((value >> shift) & 1)
            self._nbits += 1
            if self._nbits == 8:
                self.out.append(self._acc)
                self._acc = 0
                self._nbits = 0

    def finish(self):
        if self._nbits:
            self.out.append(self._acc << (8 - self._nbits))
            self._acc = 0
            self._nbits = 0
        return bytes(self.out)


class _BitReader:
    def __init__(self, data):
        self.data = data
        self.position = 0

    def bit(self):
        if self.position >= len(self.data) * 8:
            raise EOFError("ran off the end of the coded data")
        byte = self.data[self.position >> 3]
        value = (byte >> (7 - (self.position & 7))) & 1
        self.position += 1
        return value

    def at_end(self):
        # Any remaining bits must be the writer's zero padding, at most seven of them.
        return (len(self.data) * 8 - self.position) < 8


def _run_codes(length, white):
    """The code sequence for one run: zero or more make-up codes then one terminating code."""
    term = _WHITE_TERM if white else _BLACK_TERM
    makeup = _WHITE_MAKEUP if white else _BLACK_MAKEUP
    codes = []
    while length >= 2624:
        codes.append(_EXT_MAKEUP[2560])
        length -= 2560
    if length >= 64:
        base = (length // 64) * 64
        if base > 1728:
            codes.append(_EXT_MAKEUP[base if base in _EXT_MAKEUP else 1792])
            length -= base if base in _EXT_MAKEUP else 1792
            if length >= 64:
                base = (length // 64) * 64
                codes.append(makeup[base])
                length -= base
        else:
            codes.append(makeup[base])
            length -= base
    codes.append(term[length])
    return codes


def _changes(row, width):
    """Changing-element positions: every index where the colour differs from the pixel before it,
    with an implicit white pixel to the left of the row (T.4's convention)."""
    out = []
    previous = 0
    for x in range(width):
        if row[x] != previous:
            out.append(x)
            previous = row[x]
    return out


def _b1_b2(reference, a0, colour, width):
    """b1 is the first changing element on the reference line to the right of a0 with the opposite
    colour of a0's run; b2 is the next one after it. The `a0 < 0` start condition is what makes the
    first pixel of a row work, and getting it wrong shifts every row by one."""
    index = 0
    while index < len(reference) and (reference[index] <= a0 if a0 >= 0 else False):
        index += 1
    # A changing element's colour is white->black at an even index, black->white at an odd one.
    while index < len(reference) and (index % 2) != (0 if colour == 0 else 1):
        index += 1
    b1 = reference[index] if index < len(reference) else width
    b2 = reference[index + 1] if index + 1 < len(reference) else width
    return b1, b2


def encode(bitmap, width, height, k=-1, black_is_1=False):
    """Encode a bitmap to a CCITT stream.

    `bitmap` is a flat list of 0 (white) and 1 (black), row-major, `width * height` long. `k=-1`
    selects Group 4, `k=0` Group 3 one-dimensional. `black_is_1` matches /BlackIs1 in the PDF
    /DecodeParms and only changes how the bits are interpreted, never how they are coded -- so it
    is asserted here rather than silently applied.
    """
    if len(bitmap) != width * height:
        raise ValueError("bitmap is %d pixels, expected %d" % (len(bitmap), width * height))
    if k not in (-1, 0):
        raise ValueError("only K=-1 (G4) and K=0 (G3 1D) are implemented")

    writer = _BitWriter()
    reference = []
    for y in range(height):
        row = bitmap[y * width:(y + 1) * width]
        current = _changes(row, width)
        if k == 0:
            # One-dimensional: alternating runs, always starting with a white one. A row that
            # begins with a black pixel therefore emits a ZERO-LENGTH white run first -- the
            # decoder is counting on the alternation, not looking at the colours.
            position = 0
            white = True
            while position < width:
                target = 0 if white else 1
                run = 0
                while position + run < width and row[position + run] == target:
                    run += 1
                for code in _run_codes(run, white):
                    writer.put(code)
                position += run
                white = not white
        else:
            a0 = -1
            colour = 0
            while a0 < width:
                # a1 is the next changing element on THIS line to the right of a0 whose colour
                # transition opposes the current run; a2 is the one after it.
                a1 = width
                for index, change in enumerate(current):
                    if change > a0 and (index % 2) == (0 if colour == 0 else 1):
                        a1 = change
                        break
                a2 = width
                for index, change in enumerate(current):
                    if change > a1 and (index % 2) == (1 if colour == 0 else 0):
                        a2 = change
                        break
                b1, b2 = _b1_b2(reference, a0, colour, width)

                if b2 < a1:
                    writer.put(_MODE_PASS)
                    a0 = b2
                    continue
                delta = a1 - b1
                if -3 <= delta <= 3:
                    writer.put({0: _MODE_V0, 1: _MODE_VR1, 2: _MODE_VR2, 3: _MODE_VR3,
                                -1: _MODE_VL1, -2: _MODE_VL2, -3: _MODE_VL3}[delta])
                    a0 = a1
                    colour ^= 1
                else:
                    writer.put(_MODE_HORIZONTAL)
                    start = 0 if a0 < 0 else a0
                    for code in _run_codes(a1 - start, colour == 0):
                        writer.put(code)
                    for code in _run_codes(a2 - a1, colour != 0):
                        writer.put(code)
                    a0 = a2
            reference = current

    coded = writer.finish()
    restored = decode(coded, width, height, k)
    if restored != list(bitmap):
        raise AssertionError("ccitt self-check failed (k=%d, %dx%d)" % (k, width, height))
    return coded


# ---------------------------------------------------------------------------------------------
# The decoder -- the self-check's other half, written from the decoding tables rather than by
# inverting the encoder above.
# ---------------------------------------------------------------------------------------------

def _build_lookup(pairs):
    """(bit length, code) -> run length, keyed on (length, code) so a prefix read is unambiguous."""
    table = {}
    for run, (length, code) in pairs:
        table[(length, code)] = run
    return table


_WHITE_LOOKUP = _build_lookup(
    [(run, pair) for run, pair in enumerate(_WHITE_TERM)]
    + list(_WHITE_MAKEUP.items()) + list(_EXT_MAKEUP.items()))
_BLACK_LOOKUP = _build_lookup(
    [(run, pair) for run, pair in enumerate(_BLACK_TERM)]
    + list(_BLACK_MAKEUP.items()) + list(_EXT_MAKEUP.items()))


def _read_run(reader, white):
    """One complete run: make-up codes accumulate, a terminating code (< 64) ends it."""
    lookup = _WHITE_LOOKUP if white else _BLACK_LOOKUP
    total = 0
    while True:
        code = 0
        length = 0
        while True:
            code = (code << 1) | reader.bit()
            length += 1
            if length > 14:
                raise ValueError("no run code matched in 14 bits")
            if (length, code) in lookup:
                run = lookup[(length, code)]
                total += run
                break
        if run < 64:
            return total


def _read_mode(reader):
    """One 2D mode code. The codes are a prefix set, so reading one bit at a time terminates."""
    code = 0
    length = 0
    modes = {_MODE_V0: "V0", _MODE_VR1: "VR1", _MODE_VR2: "VR2", _MODE_VR3: "VR3",
             _MODE_VL1: "VL1", _MODE_VL2: "VL2", _MODE_VL3: "VL3",
             _MODE_PASS: "P", _MODE_HORIZONTAL: "H"}
    while length < 7:
        code = (code << 1) | reader.bit()
        length += 1
        if (length, code) in modes:
            return modes[(length, code)]
    raise ValueError("no 2D mode code matched in 7 bits")


def decode(data, width, height, k=-1):
    """Decode a CCITT stream back to a flat 0/1 bitmap. Independent of `encode` above."""
    reader = _BitReader(data)
    out = []
    reference = []
    for _ in range(height):
        row = [0] * width
        if k == 0:
            position = 0
            white = True
            while position < width:
                run = _read_run(reader, white)
                if not white:
                    for x in range(position, min(width, position + run)):
                        row[x] = 1
                position += run
                white = not white
        else:
            a0 = -1
            colour = 0
            while a0 < width:
                mode = _read_mode(reader)
                b1, b2 = _b1_b2(reference, a0, colour, width)
                if mode == "P":
                    if colour:
                        for x in range(max(a0, 0), b2):
                            row[x] = 1
                    a0 = b2
                elif mode == "H":
                    start = 0 if a0 < 0 else a0
                    first = _read_run(reader, colour == 0)
                    second = _read_run(reader, colour != 0)
                    if colour:
                        for x in range(start, min(width, start + first)):
                            row[x] = 1
                    else:
                        for x in range(start + first, min(width, start + first + second)):
                            row[x] = 1
                    a0 = start + first + second
                else:
                    delta = {"V0": 0, "VR1": 1, "VR2": 2, "VR3": 3,
                             "VL1": -1, "VL2": -2, "VL3": -3}[mode]
                    a1 = b1 + delta
                    if colour:
                        for x in range(max(a0, 0), min(width, a1)):
                            row[x] = 1
                    a0 = a1
                    colour ^= 1
            reference = _changes(row, width)
        out.extend(row)
    return out


if __name__ == "__main__":
    W, H = 96, 64
    # Diagonal stripes plus a filled box: stripes force vertical modes, the box's edges force
    # horizontal ones, and the run of clear rows above it forces pass mode. A single solid
    # rectangle would exercise one branch of three.
    bits = []
    for y in range(H):
        for x in range(W):
            stripe = 1 if ((x + y) // 6) % 2 == 0 else 0
            box = 1 if (20 <= x < 60 and 30 <= y < 50) else 0
            bits.append(stripe ^ box)
    for k in (-1, 0):
        blob = encode(bits, W, H, k)
        assert blob == encode(bits, W, H, k)
        assert decode(blob, W, H, k) == bits
        print("K=%-3d %5d bytes for %dx%d  (self-check ok)" % (k, len(blob), W, H))
