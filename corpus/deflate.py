#!/usr/bin/env python3
"""A deterministic DEFLATE/zlib encoder, written here rather than taken from `zlib`.

WHY NOT `zlib.compress`. The whole corpus decision (D2-11, `Stage2-Rendering-Plan.md` §3) rests on
one property: every file regenerates BYTE-IDENTICALLY from a clean checkout, and that is asserted
rather than assumed (`gates.corpus.reproducible`). `zlib.compress(data, 9)` does not give us that.
Its output is a function of the zlib build the interpreter happens to link -- match-chain limits,
lazy-match thresholds and the dynamic-Huffman cutoff are implementation choices, not format
requirements -- so the same script would emit different bytes on this Windows box and on the Ubuntu
runner, and the reproducibility gate would go red for a reason that has nothing to do with the
corpus. An encoder we own is the only version of that promise we can actually keep.

WHAT IT PRODUCES, and why all three block types are here. `pdf/cos/filters/Flate` (Stage 2 T7) has
to read whatever a real producer emits, and a corpus that only ever contains one block type leaves
the other decode paths untested:

  * STORED   -- no compression. The header/checksum path with no Huffman decoding at all.
  * FIXED    -- the RFC 1951 fixed code tables. LZ77 matches, no table transmission.
  * DYNAMIC  -- transmitted literal/length and distance code tables, themselves code-length coded.
                This is what real producers emit and it is by far the largest decoder surface.

`compress()` picks whichever of the three is smallest for the input, which makes the choice a
function of the DATA and not of the machine. Callers that want a specific block type for a fixture
say so with `mode=`.

SELF-CHECK. `compress()` round-trips its own output through `zlib.decompress` before returning it.
zlib is a genuinely independent decoder, so a bug in this file fails generation loudly instead of
producing a corpus file that only our own reader can open -- which would be the worst possible
outcome for a test corpus, since it would make the filter suite agree with itself.
"""

import zlib

MAX_MATCH = 258
MIN_MATCH = 3
WINDOW = 32768

# RFC 1951 section 3.2.5. Length codes 257..285: (extra bits, base length).
_LENGTH_TABLE = [
    (0, 3), (0, 4), (0, 5), (0, 6), (0, 7), (0, 8), (0, 9), (0, 10),
    (1, 11), (1, 13), (1, 15), (1, 17), (2, 19), (2, 23), (2, 27), (2, 31),
    (3, 35), (3, 43), (3, 51), (3, 59), (4, 67), (4, 83), (4, 99), (4, 115),
    (5, 131), (5, 163), (5, 195), (5, 227), (0, 258),
]

# Distance codes 0..29: (extra bits, base distance).
_DIST_TABLE = [
    (0, 1), (0, 2), (0, 3), (0, 4), (1, 5), (1, 7), (2, 9), (2, 13),
    (3, 17), (3, 25), (4, 33), (4, 49), (5, 65), (5, 97), (6, 129), (6, 193),
    (7, 257), (7, 385), (8, 513), (8, 769), (9, 1025), (9, 1537), (10, 2049), (10, 3073),
    (11, 4097), (11, 6145), (12, 8193), (12, 12289), (13, 16385), (13, 24577),
]

# The order in which the code-length code lengths are transmitted (RFC 1951 section 3.2.7).
_CL_ORDER = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15]


class _BitWriter:
    """LSB-first, which is DEFLATE's bit order for everything except Huffman codes."""

    def __init__(self):
        self.out = bytearray()
        self._acc = 0
        self._nbits = 0

    def bits(self, value, count):
        self._acc |= (value & ((1 << count) - 1)) << self._nbits
        self._nbits += count
        while self._nbits >= 8:
            self.out.append(self._acc & 0xFF)
            self._acc >>= 8
            self._nbits -= 8

    def huffman(self, code, length):
        """Huffman codes travel MSB-first inside an LSB-first bit stream -- RFC 1951 section 3.1.1,
        and the single most common thing to get wrong in a hand-written DEFLATE encoder."""
        for shift in range(length - 1, -1, -1):
            self.bits((code >> shift) & 1, 1)

    def finish(self):
        if self._nbits:
            self.out.append(self._acc & 0xFF)
            self._acc = 0
            self._nbits = 0
        return bytes(self.out)


def _canonical_codes(lengths):
    """RFC 1951 section 3.2.2's canonical code assignment. Deterministic given the lengths."""
    max_len = max(lengths) if lengths else 0
    count = [0] * (max_len + 1)
    for length in lengths:
        if length:
            count[length] += 1
    code = 0
    next_code = [0] * (max_len + 2)
    for bits in range(1, max_len + 1):
        code = (code + count[bits - 1]) << 1
        next_code[bits] = code
    codes = [0] * len(lengths)
    for index, length in enumerate(lengths):
        if length:
            codes[index] = next_code[length]
            next_code[length] += 1
    return codes


def _huffman_lengths(freqs, limit):
    """Code lengths for a canonical Huffman code, length-limited to `limit` bits.

    Package-merge would be the textbook answer; this is ordinary Huffman followed by a Kraft
    redistribution pass, which is what zlib itself does and is easier to read. Ties are broken by
    SYMBOL INDEX, never by heap order -- a tie broken by the interpreter's heap behaviour is
    exactly the kind of non-determinism this module exists to avoid.
    """
    used = [(freq, sym) for sym, freq in enumerate(freqs) if freq]
    if not used:
        return [0] * len(freqs)
    if len(used) == 1:
        lengths = [0] * len(freqs)
        lengths[used[0][1]] = 1
        return lengths

    # Nodes: [weight, tiebreak, symbol_or_None, left, right]. `tiebreak` makes the order total.
    nodes = [[freq, sym, sym, None, None] for freq, sym in used]
    nodes.sort(key=lambda node: (node[0], node[1]))
    counter = len(freqs)
    while len(nodes) > 1:
        left = nodes.pop(0)
        right = nodes.pop(0)
        merged = [left[0] + right[0], counter, None, left, right]
        counter += 1
        # A linear insertion rather than a heap: these alphabets are at most 288 symbols, and an
        # obvious stable insertion is worth more here than the asymptotics.
        position = len(nodes)
        for index, node in enumerate(nodes):
            if (node[0], node[1]) > (merged[0], merged[1]):
                position = index
                break
        nodes.insert(position, merged)

    lengths = [0] * len(freqs)
    stack = [(nodes[0], 0)]
    while stack:
        node, depth = stack.pop()
        if node[2] is not None:
            lengths[node[2]] = max(depth, 1)
        else:
            stack.append((node[3], depth + 1))
            stack.append((node[4], depth + 1))

    if max(lengths) <= limit:
        return lengths

    # Too deep. Two repairs, tried in order, because the first one's outputs are already in the
    # corpus and "every existing file is byte-unchanged" is a Stage 6 T1 exit criterion.
    #
    # The FIRST is this file's original: clamp everything over the limit, then lengthen the rarest
    # codes until the Kraft sum is back at or under one. When that lands exactly ON one the code is
    # complete and is kept -- `ordinary/image-flate-predictor.pdf` is one such stream. But it can
    # overshoot, and an INCOMPLETE literal/length or code-length code is refused by inflate ("invalid
    # literal/lengths set") as surely as an over-subscribed one: on annots-900.pdf's object stream
    # (an 18-deep tree) it left the sum at 0.99960 and compress()'s self-check stopped generation
    # (Stage6-Annotations-Testing-Guide.md, issue I-01).
    unclamped = list(lengths)
    for sym, length in enumerate(lengths):
        if length > limit:
            lengths[sym] = limit
    total = sum(1 << (limit - lengths[sym]) for sym in range(len(lengths)) if lengths[sym])
    one = 1 << limit
    order = sorted((sym for sym in range(len(lengths)) if lengths[sym]),
                   key=lambda sym: (freqs[sym], sym))
    index = 0
    while total > one:
        sym = order[index % len(order)]
        index += 1
        if lengths[sym] < limit:
            total -= 1 << (limit - lengths[sym])
            lengths[sym] += 1
            total += 1 << (limit - lengths[sym])
    if total == one:
        return lengths

    # The SECOND is JPEG's Adjust_BITS (ITU-T T.81 Annex K.2) on the count of codes per depth: take
    # two sibling leaves from the deepest level -- their parent becomes a leaf one level up -- and
    # hang them under a leaf from the deepest level with room, which becomes their parent. Each
    # step is surgery on a full tree that leaves it full, so the Kraft sum stays exactly one.
    deepest = max(unclamped)
    count = [0] * (deepest + 1)
    for length in unclamped:
        if length:
            count[length] += 1
    for depth in range(deepest, limit, -1):
        while count[depth] > 0:
            donor = depth - 2
            while count[donor] == 0:
                donor -= 1
            count[depth] -= 2
            count[depth - 1] += 1
            count[donor + 1] += 2
            count[donor] -= 1
    # Hand the lengths back out: the most frequent symbols take the shortest codes, ties broken by
    # symbol index -- never by sort stability or heap order.
    lengths = [0] * len(freqs)
    order = sorted((sym for sym in range(len(unclamped)) if unclamped[sym]),
                   key=lambda sym: (-freqs[sym], sym))
    position = 0
    for bits in range(1, limit + 1):
        for _ in range(count[bits]):
            lengths[order[position]] = bits
            position += 1
    if sum(1 << (limit - length) for length in lengths if length) != one:
        raise AssertionError("length-limited Huffman code is not complete")
    return lengths


def _lz77(data):
    """Greedy LZ77 over a hash chain. Emits ('lit', byte) and ('match', length, distance).

    Greedy rather than lazy: one fewer parameter to be non-deterministic about, and the corpus is
    not trying to compress well -- it is trying to produce a stream a real inflate has to decode
    correctly. The chain is capped so generating the 900-page file stays quick.
    """
    tokens = []
    heads = {}
    prev = [-1] * len(data)
    position = 0
    size = len(data)
    while position < size:
        best_length = 0
        best_distance = 0
        key = None
        if position + MIN_MATCH <= size:
            key = data[position:position + MIN_MATCH]
            candidate = heads.get(key, -1)
            chain = 0
            while candidate >= 0 and chain < 32:
                distance = position - candidate
                if distance > WINDOW:
                    break
                length = 0
                limit = min(MAX_MATCH, size - position)
                while length < limit and data[candidate + length] == data[position + length]:
                    length += 1
                if length > best_length:
                    best_length = length
                    best_distance = distance
                    if length == limit:
                        break
                candidate = prev[candidate]
                chain += 1
        if best_length >= MIN_MATCH:
            for offset in range(best_length):
                index = position + offset
                if index + MIN_MATCH <= size:
                    slot = data[index:index + MIN_MATCH]
                    prev[index] = heads.get(slot, -1)
                    heads[slot] = index
            tokens.append(("match", best_length, best_distance))
            position += best_length
        else:
            if key is not None:
                prev[position] = heads.get(key, -1)
                heads[key] = position
            tokens.append(("lit", data[position]))
            position += 1
    return tokens


def _length_code(length):
    for index, (extra, base) in enumerate(_LENGTH_TABLE):
        top = 258 if index == len(_LENGTH_TABLE) - 1 else base + (1 << extra) - 1
        if base <= length <= top:
            return 257 + index, extra, length - base
    raise ValueError("length %d out of range" % length)


def _dist_code(distance):
    for index, (extra, base) in enumerate(_DIST_TABLE):
        if base <= distance <= base + (1 << extra) - 1:
            return index, extra, distance - base
    raise ValueError("distance %d out of range" % distance)


def _emit_tokens(writer, tokens, lit_codes, lit_lengths, dist_codes, dist_lengths):
    for token in tokens:
        if token[0] == "lit":
            symbol = token[1]
            writer.huffman(lit_codes[symbol], lit_lengths[symbol])
        else:
            _, length, distance = token
            code, extra, value = _length_code(length)
            writer.huffman(lit_codes[code], lit_lengths[code])
            if extra:
                writer.bits(value, extra)
            code, extra, value = _dist_code(distance)
            writer.huffman(dist_codes[code], dist_lengths[code])
            if extra:
                writer.bits(value, extra)
    writer.huffman(lit_codes[256], lit_lengths[256])


def _fixed_tables():
    lit_lengths = [8] * 144 + [9] * 112 + [7] * 24 + [8] * 8
    dist_lengths = [5] * 30
    return lit_lengths, _canonical_codes(lit_lengths), dist_lengths, _canonical_codes(dist_lengths)


def _deflate_stored(data):
    out = bytearray()
    if not data:
        # One empty final stored block: BFINAL=1, BTYPE=00, LEN=0, NLEN=0xFFFF.
        return b"\x01\x00\x00\xff\xff"
    position = 0
    while position < len(data):
        chunk = data[position:position + 65535]
        position += len(chunk)
        final = 1 if position >= len(data) else 0
        out.append(final)  # BFINAL in bit 0, BTYPE=00 in bits 1-2, padded to the byte boundary.
        out += len(chunk).to_bytes(2, "little")
        out += (len(chunk) ^ 0xFFFF).to_bytes(2, "little")
        out += chunk
    return bytes(out)


def _deflate_fixed(data):
    writer = _BitWriter()
    writer.bits(1, 1)   # BFINAL
    writer.bits(1, 2)   # BTYPE = fixed
    lit_lengths, lit_codes, dist_lengths, dist_codes = _fixed_tables()
    _emit_tokens(writer, _lz77(data), lit_codes, lit_lengths, dist_codes, dist_lengths)
    return writer.finish()


def _deflate_dynamic(data):
    tokens = _lz77(data)
    lit_freq = [0] * 286
    dist_freq = [0] * 30
    for token in tokens:
        if token[0] == "lit":
            lit_freq[token[1]] += 1
        else:
            code, _, _ = _length_code(token[1])
            lit_freq[code] += 1
            code, _, _ = _dist_code(token[2])
            dist_freq[code] += 1
    lit_freq[256] += 1

    lit_lengths = _huffman_lengths(lit_freq, 15)
    dist_lengths = _huffman_lengths(dist_freq, 15)
    # A decoder reading HDIST=1 expects one distance code to exist, so give it one that is never
    # used rather than transmitting an empty alphabet.
    if not any(dist_lengths):
        dist_lengths[0] = 1

    hlit = 286
    while hlit > 257 and lit_lengths[hlit - 1] == 0:
        hlit -= 1
    hdist = 30
    while hdist > 1 and dist_lengths[hdist - 1] == 0:
        hdist -= 1

    combined = lit_lengths[:hlit] + dist_lengths[:hdist]

    # Run-length code the combined length sequence with symbols 16/17/18 (RFC 1951 section 3.2.7).
    cl_tokens = []
    index = 0
    while index < len(combined):
        length = combined[index]
        run = 1
        while index + run < len(combined) and combined[index + run] == length:
            run += 1
        if length == 0:
            while run >= 11:
                take = min(run, 138)
                cl_tokens.append((18, take - 11, 7))
                run -= take
                index += take
            while run >= 3:
                take = min(run, 10)
                cl_tokens.append((17, take - 3, 3))
                run -= take
                index += take
            for _ in range(run):
                cl_tokens.append((0, 0, 0))
                index += 1
        else:
            cl_tokens.append((length, 0, 0))
            index += 1
            run -= 1
            while run >= 3:
                take = min(run, 6)
                cl_tokens.append((16, take - 3, 2))
                run -= take
                index += take
            for _ in range(run):
                cl_tokens.append((length, 0, 0))
                index += 1

    cl_freq = [0] * 19
    for symbol, _, _ in cl_tokens:
        cl_freq[symbol] += 1
    cl_lengths = _huffman_lengths(cl_freq, 7)
    cl_codes = _canonical_codes(cl_lengths)

    hclen = 19
    while hclen > 4 and cl_lengths[_CL_ORDER[hclen - 1]] == 0:
        hclen -= 1

    writer = _BitWriter()
    writer.bits(1, 1)   # BFINAL
    writer.bits(2, 2)   # BTYPE = dynamic
    writer.bits(hlit - 257, 5)
    writer.bits(hdist - 1, 5)
    writer.bits(hclen - 4, 4)
    for position in range(hclen):
        writer.bits(cl_lengths[_CL_ORDER[position]], 3)
    for symbol, extra_value, extra_bits in cl_tokens:
        writer.huffman(cl_codes[symbol], cl_lengths[symbol])
        if extra_bits:
            writer.bits(extra_value, extra_bits)

    _emit_tokens(writer, tokens, _canonical_codes(lit_lengths), lit_lengths,
                 _canonical_codes(dist_lengths), dist_lengths)
    return writer.finish()


def deflate(data, mode="auto"):
    """Raw DEFLATE (no zlib wrapper). `mode` is 'auto', 'stored', 'fixed' or 'dynamic'."""
    builders = {"stored": _deflate_stored, "fixed": _deflate_fixed, "dynamic": _deflate_dynamic}
    if mode == "auto":
        names = ("stored", "fixed", "dynamic")
        candidates = [builders[name](data) for name in names]
        # Shortest wins; a tie is broken by block type in a fixed order rather than by comparing
        # compressed bytes, so the choice never depends on anything but the input's length profile.
        best = min(range(len(candidates)), key=lambda index: (len(candidates[index]), index))
        return candidates[best]
    if mode not in builders:
        raise ValueError("unknown deflate mode %r" % mode)
    return builders[mode](data)


def compress(data, mode="auto"):
    """A zlib stream (RFC 1950): two-byte header, DEFLATE data, Adler-32.

    This is what PDF's /FlateDecode wants. A bare DEFLATE stream is NOT a zlib stream and readers
    differ on whether they tolerate one, which would make the fixture a test of the reader's
    tolerance rather than of the filter.

    Verified against `zlib.decompress` before returning: if this module is wrong, generation stops
    here rather than shipping a corpus file only our own code can read.
    """
    body = deflate(data, mode)
    # CMF=0x78 (deflate, 32K window); FLG=0x9C makes the 16-bit header a multiple of 31 with the
    # level bits reading "default". 0x789C is the pairing every real producer emits.
    out = b"\x78\x9c" + body + zlib.adler32(data).to_bytes(4, "big")
    if zlib.decompress(out) != data:
        raise AssertionError("deflate self-check failed (mode=%s, %d bytes)" % (mode, len(data)))
    return out


if __name__ == "__main__":
    import hashlib

    samples = [
        b"",
        b"a",
        b"abc",
        b"RawPage " * 200,
        bytes(range(256)) * 8,
        b"BT /F1 9 Tf 48 700 Td (document render raster glyph outline baseline) Tj ET\n" * 60,
    ]
    for sample in samples:
        row = []
        for mode in ("stored", "fixed", "dynamic", "auto"):
            blob = compress(sample, mode)
            assert blob == compress(sample, mode), "not deterministic"
            row.append("%s=%d/%s" % (mode, len(blob), hashlib.sha256(blob).hexdigest()[:8]))
        print("%7d bytes -> %s" % (len(sample), "  ".join(row)))
    print("deflate self-check ok")
