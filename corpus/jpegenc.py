#!/usr/bin/env python3
"""A baseline JPEG encoder, for the corpus's /DCTDecode image streams.

WHY WE ENCODE RATHER THAN COMMIT A .jpg. D2-11 (`Stage2-Rendering-Plan.md` section 3) says every
file in `corpus/` is produced by a script we own -- so a JPEG that arrived from anywhere else is
exactly the thing the decision rules out, and a byte blob pasted into a Python source file is that
same file wearing a disguise. `pdf/cos/filters/Dct` (T7) has to read a real entropy-coded baseline
stream, so the corpus has to contain one.

WHAT IT IS. ITU-T T.81 baseline: 8-bit, sequential, Huffman, 4:4:4 (no chroma subsampling), the
Annex K example quantization and Huffman tables. Grayscale and RGB (via YCbCr) only. No progressive,
no arithmetic coding, no restart markers, no subsampling -- each of those is a genuinely different
decode path, and a generator that claims to cover them without being tested against them would be
worse than one that says plainly what it does not do. Recorded in `corpus/PROVENANCE.md` as a
corpus limitation rather than left for a reader to infer.

DETERMINISM. The forward DCT uses a HARDCODED cosine table rather than calling `math.cos`. That is
not premature optimization: `math.cos` is the platform's libm, its last bit is not specified, and a
one-ULP difference on the Ubuntu runner would round one coefficient differently and change the
file's bytes -- turning `gates.corpus.reproducible` red for a reason that has nothing to do with
this project. Everything downstream of the table is IEEE-754 addition and multiplication, which is
exactly specified.

SELF-CHECK. `encode()` verifies its own output through Pillow when Pillow is importable, comparing
the decoded image against the source within the quantization error. Pillow is NOT a build or test
dependency -- it is opportunistic, and its absence is printed rather than silently skipped, because
a self-check that quietly does nothing is Stage 1's convention 7 in a new costume.
"""

# cos((2x+1)*u*pi/16), u down, x across. Emitted once from math.cos and pinned here -- see the
# determinism note above.
_COS = [
    [1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0],
    [0.9807852804032304, 0.8314696123025452, 0.5555702330196023, 0.19509032201612833,
     -0.1950903220161282, -0.555570233019602, -0.8314696123025453, -0.9807852804032304],
    [0.9238795325112867, 0.38268343236508984, -0.3826834323650897, -0.9238795325112867,
     -0.9238795325112868, -0.38268343236509034, 0.38268343236509, 0.9238795325112865],
    [0.8314696123025452, -0.1950903220161282, -0.9807852804032304, -0.5555702330196022,
     0.5555702330196018, 0.9807852804032304, 0.19509032201612878, -0.8314696123025451],
    [0.7071067811865476, -0.7071067811865475, -0.7071067811865477, 0.7071067811865474,
     0.7071067811865477, -0.7071067811865467, -0.7071067811865471, 0.7071067811865466],
    [0.5555702330196023, -0.9807852804032304, 0.1950903220161283, 0.8314696123025456,
     -0.8314696123025451, -0.19509032201612803, 0.9807852804032307, -0.5555702330196015],
    [0.38268343236508984, -0.9238795325112868, 0.9238795325112865, -0.3826834323650899,
     -0.38268343236509056, 0.9238795325112867, -0.9238795325112864, 0.38268343236508956],
    [0.19509032201612833, -0.5555702330196022, 0.8314696123025456, -0.9807852804032307,
     0.9807852804032305, -0.831469612302545, 0.5555702330196015, -0.19509032201612858],
]

_ZIGZAG = [
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
]

# ITU-T T.81 Annex K.1, the example tables every baseline encoder starts from.
_QUANT_LUMA = [
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77,
    24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99,
]

_QUANT_CHROMA = [
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
]

# Annex K.3. `bits[i]` is how many codes are i+1 bits long; `values` is the symbol order.
_DC_LUMA_BITS = [0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0]
_DC_LUMA_VALUES = list(range(12))
_DC_CHROMA_BITS = [0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0]
_DC_CHROMA_VALUES = list(range(12))

_AC_LUMA_BITS = [0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D]
_AC_LUMA_VALUES = [
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61,
    0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52,
    0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25,
    0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64,
    0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
    0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
    0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3,
    0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8,
    0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA,
]

_AC_CHROMA_BITS = [0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77]
_AC_CHROMA_VALUES = [
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61,
    0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33,
    0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18,
    0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63,
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
    0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA,
    0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
    0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA,
]


def _huffman_table(bits, values):
    """(symbol -> (code, length)) from the BITS/HUFFVAL pair, per T.81 Annex C."""
    table = {}
    code = 0
    index = 0
    for length in range(1, 17):
        for _ in range(bits[length - 1]):
            table[values[index]] = (code, length)
            index += 1
            code += 1
        code <<= 1
    return table


class _BitWriter:
    """MSB-first, and it byte-stuffs: a 0xFF in the entropy-coded segment must be followed by 0x00
    or a decoder reads it as a marker. Forgetting the stuffing produces a file that decodes fine
    until the first saturated coefficient, which is the sort of bug a flat test image hides."""

    def __init__(self):
        self.out = bytearray()
        self._acc = 0
        self._nbits = 0

    def bits(self, value, count):
        for shift in range(count - 1, -1, -1):
            self._acc = (self._acc << 1) | ((value >> shift) & 1)
            self._nbits += 1
            if self._nbits == 8:
                self.out.append(self._acc)
                if self._acc == 0xFF:
                    self.out.append(0x00)
                self._acc = 0
                self._nbits = 0

    def flush(self):
        while self._nbits:
            self.bits(1, 1)
        return bytes(self.out)


def _fdct(block):
    """The 8x8 forward DCT, separable: rows then columns. O(N^3) and plainly written -- these are
    tiny images and a fast DCT here would only be a second place for a bug to hide."""
    rows = [[0.0] * 8 for _ in range(8)]
    for y in range(8):
        for u in range(8):
            total = 0.0
            for x in range(8):
                total += block[y * 8 + x] * _COS[u][x]
            rows[y][u] = total * (0.35355339059327373 if u == 0 else 0.5)
    out = [0.0] * 64
    for u in range(8):
        for v in range(8):
            total = 0.0
            for y in range(8):
                total += rows[y][u] * _COS[v][y]
            out[v * 8 + u] = total * (0.35355339059327373 if v == 0 else 0.5)
    return out


def _category(value):
    """The number of bits needed for a DC difference or an AC coefficient (T.81 Table F.1)."""
    magnitude = abs(value)
    size = 0
    while magnitude:
        magnitude >>= 1
        size += 1
    return size


def _encode_block(writer, block, quant, dc_table, ac_table, previous_dc):
    coefficients = _fdct(block)
    quantized = [int(round(coefficients[index] / quant[index])) for index in range(64)]
    ordered = [quantized[_ZIGZAG[index]] for index in range(64)]

    diff = ordered[0] - previous_dc
    size = _category(diff)
    code, length = dc_table[size]
    writer.bits(code, length)
    if size:
        # Negative values are transmitted as the one's complement of their magnitude (T.81 F.1.2.1).
        writer.bits(diff if diff > 0 else diff + (1 << size) - 1, size)

    run = 0
    for index in range(1, 64):
        value = ordered[index]
        if value == 0:
            run += 1
            continue
        while run > 15:
            code, length = ac_table[0xF0]   # ZRL: sixteen zeroes
            writer.bits(code, length)
            run -= 16
        size = _category(value)
        code, length = ac_table[(run << 4) | size]
        writer.bits(code, length)
        writer.bits(value if value > 0 else value + (1 << size) - 1, size)
        run = 0
    if run:
        code, length = ac_table[0x00]       # EOB
        writer.bits(code, length)
    return ordered[0]


def _segment(marker, payload):
    return bytes([0xFF, marker]) + (len(payload) + 2).to_bytes(2, "big") + payload


def encode(width, height, samples, components):
    """Encode a baseline JPEG.

    `samples` is a flat list of ints, row-major, `components` values per pixel: 1 for grayscale,
    3 for RGB (converted to YCbCr here, which is what a JPEG in a PDF with /DeviceRGB means).
    Returns the complete JFIF byte stream, SOI through EOI.
    """
    if components not in (1, 3):
        raise ValueError("only 1- and 3-component baseline JPEG is implemented")
    if len(samples) != width * height * components:
        raise ValueError("sample count does not match width*height*components")

    # Colour transform. The BT.601 coefficients JFIF specifies; integers rather than floats so the
    # conversion cannot drift by a bit between platforms.
    if components == 3:
        planes = [[0] * (width * height) for _ in range(3)]
        for index in range(width * height):
            red = samples[index * 3]
            green = samples[index * 3 + 1]
            blue = samples[index * 3 + 2]
            planes[0][index] = (299 * red + 587 * green + 114 * blue) // 1000
            planes[1][index] = 128 + (-169 * red - 331 * green + 500 * blue) // 1000
            planes[2][index] = 128 + (500 * red - 419 * green - 81 * blue) // 1000
        for plane in planes:
            for index, value in enumerate(plane):
                plane[index] = 0 if value < 0 else (255 if value > 255 else value)
    else:
        planes = [list(samples)]

    out = bytearray(b"\xff\xd8")                                  # SOI

    # APP0/JFIF. Optional for a PDF /DCTDecode stream, present because its absence is what makes a
    # reader guess at the colour transform, and a corpus file should not require guessing.
    out += _segment(0xE0, b"JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00")

    out += _segment(0xDB, bytes([0x00]) + bytes(_QUANT_LUMA[_ZIGZAG[i]] for i in range(64)))
    if components == 3:
        out += _segment(0xDB, bytes([0x01]) + bytes(_QUANT_CHROMA[_ZIGZAG[i]] for i in range(64)))

    sof = bytearray([0x08])                                       # 8-bit precision
    sof += height.to_bytes(2, "big") + width.to_bytes(2, "big")
    sof.append(components)
    for component in range(components):
        sof.append(component + 1)                                 # component id
        sof.append(0x11)                                          # 1x1 sampling -- 4:4:4
        sof.append(0 if component == 0 else 1)                    # quantization table selector
    out += _segment(0xC0, bytes(sof))

    out += _segment(0xC4, bytes([0x00]) + bytes(_DC_LUMA_BITS) + bytes(_DC_LUMA_VALUES))
    out += _segment(0xC4, bytes([0x10]) + bytes(_AC_LUMA_BITS) + bytes(_AC_LUMA_VALUES))
    if components == 3:
        out += _segment(0xC4, bytes([0x01]) + bytes(_DC_CHROMA_BITS) + bytes(_DC_CHROMA_VALUES))
        out += _segment(0xC4, bytes([0x11]) + bytes(_AC_CHROMA_BITS) + bytes(_AC_CHROMA_VALUES))

    sos = bytearray([components])
    for component in range(components):
        sos.append(component + 1)
        sos.append(0x00 if component == 0 else 0x11)              # DC/AC table selectors
    sos += bytes([0x00, 0x3F, 0x00])                              # Ss, Se, Ah/Al -- baseline
    out += _segment(0xDA, bytes(sos))

    dc_luma = _huffman_table(_DC_LUMA_BITS, _DC_LUMA_VALUES)
    ac_luma = _huffman_table(_AC_LUMA_BITS, _AC_LUMA_VALUES)
    dc_chroma = _huffman_table(_DC_CHROMA_BITS, _DC_CHROMA_VALUES)
    ac_chroma = _huffman_table(_AC_CHROMA_BITS, _AC_CHROMA_VALUES)

    writer = _BitWriter()
    previous = [0] * components
    blocks_x = (width + 7) // 8
    blocks_y = (height + 7) // 8
    for block_y in range(blocks_y):
        for block_x in range(blocks_x):
            for component in range(components):
                plane = planes[component]
                block = [0] * 64
                for y in range(8):
                    # Edge replication for a partial block. Zero-padding would put a hard edge in
                    # the DCT and produce ringing that a golden baseline would then enshrine.
                    source_y = min(block_y * 8 + y, height - 1)
                    for x in range(8):
                        source_x = min(block_x * 8 + x, width - 1)
                        block[y * 8 + x] = plane[source_y * width + source_x] - 128
                quant = _QUANT_LUMA if component == 0 else _QUANT_CHROMA
                dc_table = dc_luma if component == 0 else dc_chroma
                ac_table = ac_luma if component == 0 else ac_chroma
                previous[component] = _encode_block(
                    writer, block, quant, dc_table, ac_table, previous[component])
    out += writer.flush()
    out += b"\xff\xd9"                                            # EOI
    return bytes(out)


def selfcheck(width, height, samples, components, blob, tolerance=24):
    """Decode `blob` with Pillow, if Pillow is here, and compare against the source samples.

    Returns a one-line report. Pillow is opportunistic and deliberately not a dependency of the
    build or of the test suite -- but when it is absent the caller PRINTS that it was absent
    rather than passing quietly, because a self-check that silently does nothing is worse than no
    self-check at all (CLAUDE.md convention 7).
    """
    try:
        import io

        from PIL import Image
    except ImportError:
        return "jpeg self-check SKIPPED (Pillow not importable)"

    image = Image.open(io.BytesIO(blob))
    if image.size != (width, height):
        raise AssertionError("jpeg decoded to %r, expected %r" % (image.size, (width, height)))
    decoded = list(image.convert("RGB" if components == 3 else "L").tobytes())
    worst = 0
    for index, value in enumerate(decoded):
        worst = max(worst, abs(value - samples[index]))
    if worst > tolerance:
        raise AssertionError("jpeg round-trip off by %d (tolerance %d)" % (worst, tolerance))
    return "jpeg self-check ok (worst sample error %d of %d allowed)" % (worst, tolerance)


if __name__ == "__main__":
    # A gradient with a hard edge: the gradient exercises the AC coefficients and the edge is what
    # a flat test image would never produce.
    W, H = 64, 48
    rgb = []
    for y in range(H):
        for x in range(W):
            rgb += [x * 4, y * 5, 200 if (x // 8 + y // 8) % 2 else 40]
    blob = encode(W, H, rgb, 3)
    assert blob == encode(W, H, rgb, 3)
    print("rgb  %d bytes; %s" % (len(blob), selfcheck(W, H, rgb, 3, blob)))

    # A clamped ramp, not a wrapping one: `% 256` would put a 255-to-0 cliff in the middle of a
    # block, and a lossy codec is allowed to smear that -- so the failure would be the test image's
    # rather than the encoder's.
    gray = [min(255, x * 3 + y * 2) for y in range(H) for x in range(W)]
    blob = encode(W, H, gray, 1)
    print("gray %d bytes; %s" % (len(blob), selfcheck(W, H, gray, 1, blob)))
