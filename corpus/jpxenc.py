#!/usr/bin/env python3
"""A minimal but genuine JPEG 2000 encoder, for the corpus's /JPXDecode image stream.

WHY IT IS HERE AND WHY IT IS HARD. D2-11 (`Stage2-Rendering-Plan.md` section 3) requires the corpus
to carry a JPX stream and requires every corpus byte to come from a script we own. There is no
JPEG 2000 encoder in the Python standard library and a pasted-in blob would be a borrowed file
wearing a disguise, so the codestream is built here: MQ arithmetic coder, EBCOT tier-1 coding, tag
trees, packet headers and all.

WHAT IT PRODUCES -- stated exactly, because a codec claim nobody can check is worse than no claim:

  * A raw J2K codestream (SOC ... EOC), single tile, single quality layer, LRCP, no precincts.
  * ZERO wavelet decomposition levels. The 5/3 reversible transform is declared and applied zero
    times, so the single LL band IS the DC-level-shifted image. This is legal and it is the single
    biggest simplification here -- it removes the DWT from the encoder entirely.
  * ONE coding pass per code-block: the cleanup pass of the most significant magnitude bit plane.
    That is the first pass every EBCOT code-block starts with, so the stream is a truncated -- not
    a malformed -- codestream, and a conforming decoder reconstructs a two-tone approximation.
    The significance-propagation and magnitude-refinement passes are NOT implemented.

WHAT THE DECODED IMAGE THEREFORE LOOKS LIKE, so no later reader mistakes it for a bug: input
samples of 0 and 255 come back as roughly 0 and 128. It is a black-on-mid-grey rendering of the
source bitmap, not a photographic one. That is enough to exercise the /JPXDecode plumbing -- the
colour space, the sample depth, the image geometry, and the fact that PDFium's JPX path is reached
at all -- and it is NOT enough to be a claim about JPEG 2000 fidelity. `corpus/PROVENANCE.md`
records it that way, alongside the wider limitation D2-11 already names.

SELF-CHECK. `encode()` decodes its own output with Pillow, which links OpenJPEG -- a genuinely
independent implementation -- and asserts the geometry and the two-tone reconstruction. When
Pillow is absent the caller is told so rather than passing quietly (CLAUDE.md convention 7).
"""

# ISO/IEC 15444-1 Table C.2: (Qe, next-MPS state, next-LPS state, switch).
_QE = [
    (0x5601, 1, 1, 1), (0x3401, 2, 6, 0), (0x1801, 3, 9, 0), (0x0AC1, 4, 12, 0),
    (0x0521, 5, 29, 0), (0x0221, 38, 33, 0), (0x5601, 7, 6, 1), (0x5401, 8, 14, 0),
    (0x4801, 9, 14, 0), (0x3801, 10, 14, 0), (0x3001, 11, 17, 0), (0x2401, 12, 18, 0),
    (0x1C01, 13, 20, 0), (0x1601, 29, 21, 0), (0x5601, 15, 14, 1), (0x5401, 16, 14, 0),
    (0x5101, 17, 15, 0), (0x4801, 18, 16, 0), (0x3801, 19, 17, 0), (0x3401, 20, 18, 0),
    (0x3001, 21, 19, 0), (0x2801, 22, 19, 0), (0x2401, 23, 20, 0), (0x2201, 24, 21, 0),
    (0x1C01, 25, 22, 0), (0x1801, 26, 23, 0), (0x1601, 27, 24, 0), (0x1401, 28, 25, 0),
    (0x1201, 29, 26, 0), (0x1101, 30, 27, 0), (0x0AC1, 31, 28, 0), (0x09C1, 32, 29, 0),
    (0x08A1, 33, 30, 0), (0x0521, 34, 31, 0), (0x0441, 35, 32, 0), (0x02A1, 36, 33, 0),
    (0x0221, 37, 34, 0), (0x0141, 38, 35, 0), (0x0111, 39, 36, 0), (0x0085, 40, 37, 0),
    (0x0049, 41, 38, 0), (0x0025, 42, 39, 0), (0x0015, 43, 40, 0), (0x0009, 44, 41, 0),
    (0x0005, 45, 42, 0), (0x0001, 45, 43, 0), (0x5601, 46, 46, 0),
]

_CTX_RUNLENGTH = 17
_CTX_UNIFORM = 18


class MQEncoder:
    """The MQ arithmetic coder of ISO/IEC 15444-1 Annex C, in its software-conventions form.

    The byte buffer starts with one dummy byte so that the spec's "BP points one before the start"
    initialization has a real byte to read; `finish()` drops it. Doing it any other way means
    special-casing the first carry, which is where hand-written MQ coders go wrong.
    """

    def __init__(self, context_count=19):
        self.out = bytearray([0])          # out[0] is the dummy; bp indexes into out
        self.bp = 0
        self.a = 0x8000
        self.c = 0
        self.ct = 12
        self.contexts = [(0, 0)] * context_count
        # Table D.7's non-zero initial states. Everything else starts at state 0, MPS 0.
        self.contexts[0] = (4, 0)
        self.contexts[_CTX_RUNLENGTH] = (3, 0)
        self.contexts[_CTX_UNIFORM] = (46, 0)

    def _byteout(self):
        if self.out[self.bp] == 0xFF:
            self.bp += 1
            self.out.append((self.c >> 20) & 0xFF)
            self.c &= 0xFFFFF
            self.ct = 7
        elif (self.c & 0x8000000) == 0:
            self.bp += 1
            self.out.append((self.c >> 19) & 0xFF)
            self.c &= 0x7FFFF
            self.ct = 8
        else:
            self.out[self.bp] += 1
            if self.out[self.bp] == 0xFF:
                self.c &= 0x7FFFFFF
                self.bp += 1
                self.out.append((self.c >> 20) & 0xFF)
                self.c &= 0xFFFFF
                self.ct = 7
            else:
                self.bp += 1
                self.out.append((self.c >> 19) & 0xFF)
                self.c &= 0x7FFFF
                self.ct = 8

    def _renorm(self):
        while True:
            self.a = (self.a << 1) & 0xFFFF
            self.c = (self.c << 1) & 0xFFFFFFF
            self.ct -= 1
            if self.ct == 0:
                self._byteout()
            if self.a & 0x8000:
                break

    def encode(self, context, decision):
        state, mps = self.contexts[context]
        qe, nmps, nlps, switch = _QE[state]
        if decision == mps:
            self.a -= qe
            if (self.a & 0x8000) == 0:
                if self.a < qe:
                    self.a = qe
                else:
                    self.c += qe
                self.contexts[context] = (nmps, mps)
                self._renorm()
            else:
                self.c += qe
        else:
            self.a -= qe
            if self.a < qe:
                self.c += qe
            else:
                self.a = qe
            if switch:
                mps = 1 - mps
            self.contexts[context] = (nlps, mps)
            self._renorm()

    def finish(self):
        """SETBITS + two BYTEOUTs, then drop a trailing 0xFF. A codestream may not contain 0xFF
        followed by a byte above 0x8F, which is why the spec's optional 0xFF 0xAC terminator is
        not written here."""
        temp = self.c + self.a
        self.c |= 0xFFFF
        if self.c >= temp:
            self.c -= 0x8000
        self.c = (self.c << self.ct) & 0xFFFFFFF
        self._byteout()
        self.c = (self.c << self.ct) & 0xFFFFFFF
        self._byteout()
        data = bytes(self.out[1:self.bp + 1])
        if data.endswith(b"\xff"):
            data = data[:-1]
        return data


class _HeaderWriter:
    """The packet-header bit writer, with the bit stuffing of section B.10.1: the byte after an
    0xFF carries only seven bits, its top bit forced to zero. Getting this wrong produces a header
    that decodes correctly until the first 0xFF, which for a small image may be never."""

    def __init__(self):
        self.out = bytearray()
        self.cur = 0
        self.n = 0
        self.cap = 8

    def bit(self, value):
        if self.n == self.cap:
            self.out.append(self.cur)
            self.cap = 7 if self.cur == 0xFF else 8
            self.cur = 0
            self.n = 0
        self.cur = (self.cur << 1) | (value & 1)
        self.n += 1

    def bits(self, value, count):
        for shift in range(count - 1, -1, -1):
            self.bit((value >> shift) & 1)

    def finish(self):
        if self.n:
            self.out.append(self.cur << (self.cap - self.n))
        if self.out and self.out[-1] == 0xFF:
            self.out.append(0x00)
        return bytes(self.out)


class _TagNode:
    """One node of a tag tree. The corpus's images are a single code-block per band, so every tag
    tree here has exactly one node -- but the encoding procedure is the general one from section
    B.10.2, because a one-node special case would be a different algorithm wearing the same name."""

    def __init__(self, value):
        self.value = value
        self.low = 0
        self.known = False


def _tag_encode(writer, node, threshold):
    low = node.low
    while low < threshold:
        if low >= node.value:
            if not node.known:
                writer.bit(1)
                node.known = True
            break
        writer.bit(0)
        low += 1
    node.low = low


def _comma_code(writer, count):
    """`count` ones then a zero -- how a Lblock increase is signalled (section B.10.7.1)."""
    for _ in range(count):
        writer.bit(1)
    writer.bit(0)


def _floor_log2(value):
    return value.bit_length() - 1


def _zero_context(significance, x, y, width, height):
    """Table D.1's zero-coding context for the LL band. The corpus uses zero decomposition levels,
    so LL is the only band that exists and the LH/HL/HH columns of that table are unreachable
    here -- which is stated rather than silently omitted."""
    def sig(dx, dy):
        nx, ny = x + dx, y + dy
        if 0 <= nx < width and 0 <= ny < height:
            return 1 if significance[ny * width + nx] else 0
        return 0

    h = sig(-1, 0) + sig(1, 0)
    v = sig(0, -1) + sig(0, 1)
    d = sig(-1, -1) + sig(1, -1) + sig(-1, 1) + sig(1, 1)
    if h == 2:
        return 8
    if h == 1:
        if v >= 1:
            return 7
        return 6 if d >= 1 else 5
    if v == 2:
        return 4
    if v == 1:
        return 3
    if d >= 2:
        return 2
    return 1 if d == 1 else 0


# Table D.3: (horizontal contribution, vertical contribution) -> (context, XOR bit).
_SIGN_TABLE = {
    (1, 1): (13, 0), (1, 0): (12, 0), (1, -1): (11, 0),
    (0, 1): (10, 0), (0, 0): (9, 0), (0, -1): (10, 1),
    (-1, 1): (11, 1), (-1, 0): (12, 1), (-1, -1): (13, 1),
}


def _sign_context(significance, signs, x, y, width, height):
    def contribution(dx, dy):
        nx, ny = x + dx, y + dy
        if 0 <= nx < width and 0 <= ny < height and significance[ny * width + nx]:
            return -1 if signs[ny * width + nx] else 1
        return 0

    h = contribution(-1, 0) + contribution(1, 0)
    v = contribution(0, -1) + contribution(0, 1)
    h = max(-1, min(1, h))
    v = max(-1, min(1, v))
    return _SIGN_TABLE[(h, v)]


def _cleanup_pass(coefficients, width, height, plane):
    """The cleanup pass of one bit plane over one code-block (section D.4.4).

    Because this is the FIRST pass of the MOST SIGNIFICANT plane, nothing is significant on entry
    and no coefficient has been visited by an earlier pass -- which is exactly why only this pass
    is implemented. Every branch below is reachable in that state; none is a stub.
    """
    coder = MQEncoder()
    significance = [0] * (width * height)
    signs = [1 if value < 0 else 0 for value in coefficients]
    magnitudes = [abs(value) for value in coefficients]
    mask = 1 << plane

    for stripe in range(0, height, 4):
        rows = min(4, height - stripe)
        for x in range(width):
            start = 0
            # Run-length mode: a full four-row column, nothing significant, every context zero.
            if rows == 4:
                aggregate_possible = all(
                    _zero_context(significance, x, stripe + i, width, height) == 0
                    for i in range(4))
            else:
                aggregate_possible = False
            if aggregate_possible:
                any_set = any(magnitudes[(stripe + i) * width + x] & mask for i in range(4))
                coder.encode(_CTX_RUNLENGTH, 1 if any_set else 0)
                if not any_set:
                    continue
                first = next(i for i in range(4) if magnitudes[(stripe + i) * width + x] & mask)
                coder.encode(_CTX_UNIFORM, (first >> 1) & 1)
                coder.encode(_CTX_UNIFORM, first & 1)
                index = (stripe + first) * width + x
                significance[index] = 1
                context, flip = _sign_context(significance, signs, x, stripe + first,
                                              width, height)
                coder.encode(context, signs[index] ^ flip)
                start = first + 1

            for i in range(start, rows):
                y = stripe + i
                index = y * width + x
                context = _zero_context(significance, x, y, width, height)
                bit = 1 if magnitudes[index] & mask else 0
                coder.encode(context, bit)
                if bit:
                    significance[index] = 1
                    sign_context, flip = _sign_context(significance, signs, x, y, width, height)
                    coder.encode(sign_context, signs[index] ^ flip)

    return coder.finish()


def _marker(code, payload):
    """A marker segment: the two-byte code, a two-byte length that counts itself, the payload."""
    return bytes([0xFF, code]) + (len(payload) + 2).to_bytes(2, "big") + payload


def encode(width, height, samples, components):
    """Encode a J2K codestream. `samples` is a flat row-major list of 8-bit values, `components`
    per pixel (1 or 3). The image must fit in one 64x64 code-block."""
    if components not in (1, 3):
        raise ValueError("only 1- and 3-component images are implemented")
    if width > 64 or height > 64:
        raise ValueError("this encoder emits one code-block; keep the image within 64x64")
    if len(samples) != width * height * components:
        raise ValueError("sample count does not match width*height*components")

    guard_bits = 2
    exponent = 8
    band_bits = guard_bits + exponent - 1            # Mb, section E.1

    out = bytearray(b"\xff\x4f")                     # SOC

    siz = bytearray(b"\x00\x00")                     # Rsiz: no extensions
    siz += width.to_bytes(4, "big") + height.to_bytes(4, "big")
    siz += (0).to_bytes(4, "big") + (0).to_bytes(4, "big")       # image offset
    siz += width.to_bytes(4, "big") + height.to_bytes(4, "big")  # one tile covering the image
    siz += (0).to_bytes(4, "big") + (0).to_bytes(4, "big")       # tile offset
    siz += components.to_bytes(2, "big")
    for _ in range(components):
        siz += bytes([0x07, 0x01, 0x01])             # 8-bit unsigned, no subsampling
    out += _marker(0x51, bytes(siz))

    cod = bytearray([0x00])                          # Scod: no precincts, no SOP, no EPH
    cod += bytes([0x00])                             # progression order: LRCP
    cod += (1).to_bytes(2, "big")                    # one quality layer
    cod += bytes([0x00])                             # no multiple-component transform
    cod += bytes([0x00])                             # zero decomposition levels
    cod += bytes([0x04, 0x04])                       # code-block 64x64 (exponent - 2)
    cod += bytes([0x00])                             # no code-block style options
    cod += bytes([0x01])                             # 5/3 reversible
    out += _marker(0x52, bytes(cod))

    qcd = bytearray([(guard_bits << 5) | 0x00])      # no quantization, reversible
    qcd += bytes([exponent << 3])                    # one band: the LL
    out += _marker(0x5C, bytes(qcd))

    # --- the tile ---------------------------------------------------------------------------
    body = bytearray()
    for component in range(components):
        plane = [samples[index * components + component] - 128
                 for index in range(width * height)]
        highest = max((abs(value).bit_length() for value in plane), default=0)
        missing = band_bits - highest
        if missing < 0:
            raise ValueError("coefficient needs more bit planes than the band declares")
        data = _cleanup_pass(plane, width, height, highest - 1) if highest else b""

        header = _HeaderWriter()
        if not data:
            header.bit(0)                            # an empty packet
        else:
            header.bit(1)                            # this packet is non-empty
            _tag_encode(header, _TagNode(0), 1)      # inclusion: first layer
            imsb = _TagNode(missing)
            threshold = 1
            while not imsb.known:
                _tag_encode(header, imsb, threshold)
                threshold += 1
            header.bit(0)                            # one coding pass (Table B.4)
            length_bits = 3                          # Lblock starts at 3
            increment = max(0, _floor_log2(len(data)) + 1 - length_bits)
            _comma_code(header, increment)
            header.bits(len(data), length_bits + increment)
        body += header.finish()
        body += data

    sot = bytearray((0).to_bytes(2, "big"))          # Isot: tile 0
    # Psot counts SOT's marker and length through the end of the tile-part data.
    psot = 2 + 10 + 2 + len(body)
    sot += psot.to_bytes(4, "big")
    sot += bytes([0x00, 0x01])                       # tile-part 0 of 1
    out += _marker(0x90, bytes(sot))
    out += b"\xff\x93"                               # SOD
    out += body
    out += b"\xff\xd9"                               # EOC
    return bytes(out)


def selfcheck(width, height, samples, components, blob):
    """Decode with Pillow (OpenJPEG) and assert the geometry and the two-tone reconstruction."""
    try:
        import io

        from PIL import Image
    except ImportError:
        return "jpx self-check SKIPPED (Pillow not importable)"

    image = Image.open(io.BytesIO(blob), formats=["JPEG2000"])
    image.load()
    if image.size != (width, height):
        raise AssertionError("jpx decoded to %r, expected %r" % (image.size, (width, height)))
    decoded = list(image.convert("L").tobytes())
    dark = [decoded[i] for i in range(len(decoded))
            if samples[i * components] < 128]
    light = [decoded[i] for i in range(len(decoded))
             if samples[i * components] >= 128]
    # The one-pass reconstruction collapses to two levels; assert the ORDER survives rather than
    # exact values, because the exact values are a property of the truncation, not of the source.
    if dark and light and max(dark) >= min(light):
        raise AssertionError("jpx round-trip lost the two-tone ordering: dark<=%d light>=%d"
                             % (max(dark), min(light)))
    return "jpx self-check ok (dark<=%d, light>=%d)" % (max(dark or [0]), min(light or [255]))


if __name__ == "__main__":
    W, H = 64, 64
    bits = []
    for y in range(H):
        for x in range(W):
            ring = 1 if 14 * 14 <= (x - 32) ** 2 + (y - 32) ** 2 <= 24 * 24 else 0
            bar = 1 if (4 <= y < 10 and 8 <= x < 56) else 0
            bits.append(0 if (ring or bar) else 255)
    blob = encode(W, H, bits, 1)
    assert blob == encode(W, H, bits, 1), "not deterministic"
    print("j2k %d bytes; %s" % (len(blob), selfcheck(W, H, bits, 1, blob)))
