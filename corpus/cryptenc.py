#!/usr/bin/env python3
"""The PDF standard security handler, encrypt side -- for `corpus/encrypted/`.

WHY THE CORPUS NEEDS THESE. Stage 2's exit criteria say every corpus file opens, and D2-6
(`Stage2-Rendering-Plan.md` section 3) settles how: an encrypted document is read through PDFium's
password callback, while the first-party COS reader DECLINES it by name until Stage 10 builds
`StandardSecurityHandler`. Both halves need a file that is genuinely encrypted -- a file that merely
carries an /Encrypt dictionary would let the COS reader's decline look right while PDFium failed,
and the two results would be indistinguishable from the correct pair.

WHAT IS IMPLEMENTED. The four revisions a reader actually meets:

    R2  V1  RC4, 40-bit          the original, still produced by old tools
    R3  V2  RC4, 128-bit
    R4  V4  AES-128 (AESV2)      crypt filters arrive here
    R6  V5  AES-256 (AESV3)      ISO 32000-2's, and the only one not built on MD5

AES IS WRITTEN OUT HERE rather than imported. `cryptography` is on this machine and is NOT a
dependency of this project -- generation must work from a bare Python -- so the cipher is
first-party and `cryptography` is used, when present, only to CHECK it. That check is the whole
reason writing it out is safe: a wrong AES would produce a file nobody can open, and the failure
would show up three tasks later as "PDFium cannot read our encrypted fixture".

THIS IS NOT A SECURITY COMPONENT. It generates test data. `pdf/cos/StandardSecurityHandler` at
Stage 10 is the real one, is C++, and shares no code with this file -- so nothing here is on any
path a user's document takes.
"""

import hashlib
import struct

# ISO 32000-1 Algorithm 2's padding string -- 32 fixed bytes appended to every password.
_PAD = bytes([
    0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
    0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A,
])


# ---------------------------------------------------------------------------------------------
# RC4
# ---------------------------------------------------------------------------------------------

def rc4(key, data):
    state = list(range(256))
    j = 0
    for i in range(256):
        j = (j + state[i] + key[i % len(key)]) & 0xFF
        state[i], state[j] = state[j], state[i]
    out = bytearray()
    i = j = 0
    for byte in data:
        i = (i + 1) & 0xFF
        j = (j + state[i]) & 0xFF
        state[i], state[j] = state[j], state[i]
        out.append(byte ^ state[(state[i] + state[j]) & 0xFF])
    return bytes(out)


# ---------------------------------------------------------------------------------------------
# AES -- encryption direction only, which is all a generator needs
# ---------------------------------------------------------------------------------------------

def _build_tables():
    sbox = [0] * 256
    p = q = 1
    while True:
        # Multiply p by 3 and divide q by 3 in GF(2^8); the classic table construction.
        p = p ^ ((p << 1) & 0xFF) ^ (0x1B if p & 0x80 else 0)
        q ^= q << 1
        q ^= q << 2
        q ^= q << 4
        q &= 0xFF
        if q & 0x80:
            q ^= 0x09
        value = q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) \
            ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4))
        sbox[p] = (value ^ 0x63) & 0xFF
        if p == 1:
            break
    sbox[0] = 0x63
    return sbox


_SBOX = _build_tables()
_RCON = [0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36, 0x6C, 0xD8, 0xAB, 0x4D]


def _xtime(value):
    value <<= 1
    return (value ^ 0x1B) & 0xFF if value & 0x100 else value


def _expand_key(key):
    words = [list(key[i * 4:i * 4 + 4]) for i in range(len(key) // 4)]
    nk = len(words)
    rounds = {4: 10, 6: 12, 8: 14}[nk]
    for i in range(nk, 4 * (rounds + 1)):
        temp = list(words[i - 1])
        if i % nk == 0:
            temp = temp[1:] + temp[:1]
            temp = [_SBOX[byte] for byte in temp]
            temp[0] ^= _RCON[i // nk - 1]
        elif nk > 6 and i % nk == 4:
            temp = [_SBOX[byte] for byte in temp]
        words.append([words[i - nk][j] ^ temp[j] for j in range(4)])
    return words, rounds


def _encrypt_block(block, words, rounds):
    state = [list(block[row::4]) for row in range(4)]   # column-major to row-major

    def add_round_key(index):
        for column in range(4):
            word = words[index * 4 + column]
            for row in range(4):
                state[row][column] ^= word[row]

    add_round_key(0)
    for rnd in range(1, rounds + 1):
        for row in range(4):
            for column in range(4):
                state[row][column] = _SBOX[state[row][column]]
        for row in range(1, 4):
            state[row] = state[row][row:] + state[row][:row]
        if rnd != rounds:
            for column in range(4):
                a = [state[row][column] for row in range(4)]
                b = [_xtime(value) for value in a]
                state[0][column] = b[0] ^ a[1] ^ b[1] ^ a[2] ^ a[3]
                state[1][column] = a[0] ^ b[1] ^ a[2] ^ b[2] ^ a[3]
                state[2][column] = a[0] ^ a[1] ^ b[2] ^ a[3] ^ b[3]
                state[3][column] = a[0] ^ b[0] ^ a[1] ^ a[2] ^ b[3]
        add_round_key(rnd)

    out = bytearray(16)
    for column in range(4):
        for row in range(4):
            out[column * 4 + row] = state[row][column]
    return bytes(out)


def aes_cbc_encrypt(key, iv, data, pad=True):
    """AES-CBC. `pad=True` applies PKCS#7, which is what a PDF stream wants; the R6 key wrapping
    uses `pad=False` because the spec wraps exactly 32 bytes with no padding."""
    words, rounds = _expand_key(key)
    if pad:
        padding = 16 - len(data) % 16
        data = data + bytes([padding]) * padding
    elif len(data) % 16:
        raise ValueError("unpadded CBC needs a multiple of 16 bytes")
    out = bytearray()
    previous = iv
    for index in range(0, len(data), 16):
        block = bytes(a ^ b for a, b in zip(data[index:index + 16], previous))
        previous = _encrypt_block(block, words, rounds)
        out += previous
    return bytes(out)


def aes_ecb_encrypt(key, data):
    words, rounds = _expand_key(key)
    if len(data) % 16:
        raise ValueError("ECB needs a multiple of 16 bytes")
    return b"".join(_encrypt_block(data[i:i + 16], words, rounds)
                    for i in range(0, len(data), 16))


def _aes_selfcheck():
    """FIPS-197 Appendix C's known-answer vectors, then a comparison against `cryptography` when
    it is importable. The KATs are the load-bearing half: they hold on a machine with no
    third-party packages at all."""
    plain = bytes.fromhex("00112233445566778899aabbccddeeff")
    vectors = [
        (bytes(range(0x00, 0x10)), "69c4e0d86a7b0430d8cdb78070b4c55a"),
        (bytes(range(0x00, 0x18)), "dda97ca4864cdfe06eaf70a0ec0d7191"),
        (bytes(range(0x00, 0x20)), "8ea2b7ca516745bfeafc49904b496089"),
    ]
    for key, expected in vectors:
        words, rounds = _expand_key(key)
        got = _encrypt_block(plain, words, rounds).hex()
        if got != expected:
            raise AssertionError("AES-%d KAT failed: %s != %s" % (len(key) * 8, got, expected))
    try:
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    except ImportError:
        return "aes self-check ok (FIPS-197 vectors; `cryptography` not importable)"
    key = bytes(range(32))
    iv = bytes(range(16, 32))
    message = b"RawPage corpus encryption self-check payload." * 3
    mine = aes_cbc_encrypt(key, iv, message)
    encryptor = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    padding = 16 - len(message) % 16
    theirs = encryptor.update(message + bytes([padding]) * padding) + encryptor.finalize()
    if mine != theirs:
        raise AssertionError("AES-CBC disagrees with `cryptography`")
    return "aes self-check ok (FIPS-197 vectors and `cryptography` agree)"


# ---------------------------------------------------------------------------------------------
# The standard security handler
# ---------------------------------------------------------------------------------------------

def _pad_password(password):
    raw = password.encode("latin-1")[:32]
    return raw + _PAD[:32 - len(raw)]


def _owner_entry(owner, user, revision, key_length):
    """Algorithm 3. The /O entry is the USER password encrypted under a key derived from the
    OWNER password -- which is why an owner password alone can open the file."""
    digest = hashlib.md5(_pad_password(owner or user)).digest()
    if revision >= 3:
        for _ in range(50):
            digest = hashlib.md5(digest).digest()
    key = digest[:key_length]
    out = rc4(key, _pad_password(user))
    if revision >= 3:
        for round_index in range(1, 20):
            out = rc4(bytes(byte ^ round_index for byte in key), out)
    return out


def _file_key(user, owner_entry, permissions, file_id, revision, key_length,
              encrypt_metadata=True):
    """Algorithm 2."""
    digest = hashlib.md5()
    digest.update(_pad_password(user))
    digest.update(owner_entry)
    digest.update(struct.pack("<i", permissions))
    digest.update(file_id)
    if revision >= 4 and not encrypt_metadata:
        digest.update(b"\xff\xff\xff\xff")
    key = digest.digest()
    if revision >= 3:
        for _ in range(50):
            key = hashlib.md5(key[:key_length]).digest()
    return key[:key_length]


def _user_entry(key, file_id, revision):
    """Algorithms 4 (R2) and 5 (R3+)."""
    if revision == 2:
        return rc4(key, _PAD)
    digest = hashlib.md5(_PAD + file_id).digest()
    out = rc4(key, digest)
    for round_index in range(1, 20):
        out = rc4(bytes(byte ^ round_index for byte in key), out)
    # The trailing 16 bytes are arbitrary. A constant rather than random data, because the corpus
    # regenerates byte-identically and "arbitrary" cannot mean "different every run".
    return out + bytes(16)


def _object_key(key, number, generation, aes):
    extra = b"sAlT" if aes else b""
    digest = hashlib.md5(
        key + struct.pack("<I", number)[:3] + struct.pack("<I", generation)[:2] + extra).digest()
    return digest[:min(len(key) + 5, 16)]


def _hash_2b(password, salt, extra):
    """ISO 32000-2 Algorithm 2.B: the iterated SHA-2/AES hash R6 uses instead of MD5."""
    digest = hashlib.sha256(password + salt + extra).digest()
    round_index = 0
    while True:
        block = (password + digest + extra) * 64
        encrypted = aes_cbc_encrypt(digest[:16], digest[16:32], block, pad=False)
        modulo = sum(encrypted[:16]) % 3
        digest = [hashlib.sha256, hashlib.sha384, hashlib.sha512][modulo](encrypted).digest()
        round_index += 1
        if round_index >= 64 and encrypted[-1] <= round_index - 32:
            break
    return digest[:32]


class Encryptor:
    """Holds the file key and produces the /Encrypt dictionary plus a per-object encrypt callable.

    `Pdf.encryptor` is set to `self.encrypt_stream`; strings are handed to `encrypt_string`
    explicitly by the generator, because a builder that went looking for string literals inside
    already-assembled dictionary bytes would be guessing.
    """

    def __init__(self, revision, user="user", owner="owner", permissions=-4,
                 file_id=b"\x00" * 16):
        self.revision = revision
        self.permissions = permissions
        self.file_id = file_id
        self.aes = revision >= 4

        if revision == 6:
            # A fixed "random" file key and fixed salts: the corpus must regenerate identically,
            # so every value the spec calls random is a constant here and is named as such.
            self.key = bytes((index * 7 + 11) & 0xFF for index in range(32))
            user_validation = b"USERVSLT"
            user_key_salt = b"USERKSLT"
            owner_validation = b"OWNRVSLT"
            owner_key_salt = b"OWNRKSLT"
            user_bytes = user.encode("utf-8")
            owner_bytes = owner.encode("utf-8")
            self.u = (_hash_2b(user_bytes, user_validation, b"")
                      + user_validation + user_key_salt)
            self.ue = aes_cbc_encrypt(_hash_2b(user_bytes, user_key_salt, b""), bytes(16),
                                      self.key, pad=False)
            self.o = (_hash_2b(owner_bytes, owner_validation, self.u)
                      + owner_validation + owner_key_salt)
            self.oe = aes_cbc_encrypt(_hash_2b(owner_bytes, owner_key_salt, self.u), bytes(16),
                                      self.key, pad=False)
            perms = struct.pack("<i", permissions) + b"\xff\xff\xff\xff" + b"Tadb" + b"rawp"
            self.perms = aes_ecb_encrypt(self.key, perms)
        else:
            self.key_length = 5 if revision == 2 else 16
            self.o = _owner_entry(owner, user, revision, self.key_length)
            self.key = _file_key(user, self.o, permissions, file_id, revision, self.key_length)
            self.u = _user_entry(self.key, file_id, revision)

    def encrypt_stream(self, number, generation, data):
        if self.revision == 6:
            # AES-256 uses the file key directly -- there is no per-object key derivation.
            iv = bytes(((number * 31 + generation * 17 + index) & 0xFF) for index in range(16))
            return iv + aes_cbc_encrypt(self.key, iv, data)
        key = _object_key(self.key, number, generation, self.aes)
        if self.aes:
            iv = bytes(((number * 13 + index * 7) & 0xFF) for index in range(16))
            return iv + aes_cbc_encrypt(key, iv, data)
        return rc4(key, data)

    def encrypt_string(self, number, generation, data):
        return self.encrypt_stream(number, generation, data)

    def dictionary_pairs(self):
        """The /Encrypt dictionary, as (key, value) byte pairs for `pdfbuild.dictionary`."""
        import pdfbuild

        if self.revision == 6:
            crypt = pdfbuild.dictionary([
                (b"/StdCF", pdfbuild.dictionary([
                    (b"/CFM", b"/AESV3"), (b"/AuthEvent", b"/DocOpen"),
                    (b"/Length", b"32")]))])
            return [(b"/Filter", b"/Standard"), (b"/V", b"5"), (b"/R", b"6"),
                    (b"/Length", b"256"), (b"/CF", crypt),
                    (b"/StmF", b"/StdCF"), (b"/StrF", b"/StdCF"),
                    (b"/O", pdfbuild.hexstring(self.o)), (b"/U", pdfbuild.hexstring(self.u)),
                    (b"/OE", pdfbuild.hexstring(self.oe)), (b"/UE", pdfbuild.hexstring(self.ue)),
                    (b"/Perms", pdfbuild.hexstring(self.perms)),
                    (b"/P", b"%d" % self.permissions)]
        if self.revision == 4:
            crypt = pdfbuild.dictionary([
                (b"/StdCF", pdfbuild.dictionary([
                    (b"/CFM", b"/AESV2"), (b"/AuthEvent", b"/DocOpen"),
                    (b"/Length", b"16")]))])
            return [(b"/Filter", b"/Standard"), (b"/V", b"4"), (b"/R", b"4"),
                    (b"/Length", b"128"), (b"/CF", crypt),
                    (b"/StmF", b"/StdCF"), (b"/StrF", b"/StdCF"),
                    (b"/O", pdfbuild.hexstring(self.o)), (b"/U", pdfbuild.hexstring(self.u)),
                    (b"/P", b"%d" % self.permissions)]
        if self.revision == 3:
            return [(b"/Filter", b"/Standard"), (b"/V", b"2"), (b"/R", b"3"),
                    (b"/Length", b"128"),
                    (b"/O", pdfbuild.hexstring(self.o)), (b"/U", pdfbuild.hexstring(self.u)),
                    (b"/P", b"%d" % self.permissions)]
        return [(b"/Filter", b"/Standard"), (b"/V", b"1"), (b"/R", b"2"),
                (b"/O", pdfbuild.hexstring(self.o)), (b"/U", pdfbuild.hexstring(self.u)),
                (b"/P", b"%d" % self.permissions)]


if __name__ == "__main__":
    print(_aes_selfcheck())
    assert rc4(b"Key", b"Plaintext").hex() == "bbf316e8d940af0ad3", "RC4 KAT failed"
    print("rc4 self-check ok (RFC 6229 / Wikipedia vector)")
    for revision in (2, 3, 4, 6):
        one = Encryptor(revision)
        two = Encryptor(revision)
        assert one.dictionary_pairs() == two.dictionary_pairs(), "R%d not deterministic" % revision
        print("R%d /Encrypt built, O=%s..." % (revision, one.o[:6].hex()))
