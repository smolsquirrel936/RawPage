#!/usr/bin/env python3
"""Detached CMS signatures, sign side -- for `corpus/signed/`. Stage 3 T1, decision D3-2.

WHY THE CORPUS NEEDS THESE. Risk A4 (`Tech-Stack-Plan.md` section 3.3) asks whether an incremental
save leaves a pre-existing signature valid, and D3-2 (`Stage3-Mutation-Plan.md` section 3) settles
how that is judged: over signed files GENERATED like every other corpus file (D2-11), by two
validators that share no code with RawPage -- the OpenSSL command-line tool for the integrity of
the signed range, Adobe Acrobat for modification policy. A signature is only a fixture if it is
real, so everything a verifier checks is really here: the digest over `/ByteRange`, the signed
attributes, an RSA signature over them, and the certificate that carries the public key.

WHAT IS IMPLEMENTED, and nothing more:

    RSA-2048          PKCS#1 v1.5 signatures, e = 65537, the key DERIVED from a seed in this file
    SHA-256           the digest everywhere -- hashlib, which is FIPS 180-4 and the same on every host
    X.509 v3          one self-signed certificate: basicConstraints, keyUsage, subjectKeyIdentifier
    CMS SignedData    RFC 5652, detached, one SignerInfo, signed attributes contentType,
                      signingTime and messageDigest -- what `/SubFilter /adbe.pkcs7.detached` carries

NO PRIVATE KEY IS COMMITTED, and that is the reason the key is derived rather than stored.
`corpus/PROVENANCE.md` has said since Stage 2 that *a private key committed to a repository is a bad
habit even when it protects nothing*. The key below is re-derived from `SEED` at generation time and
lives only in memory; the seed is public, so the key is public, and the certificate's subject says
in words that it is a corpus test identity. Nothing signed by it means anything, which is the point.

WHY DETERMINISM HOLDS. `gates.corpus.reproducible` requires byte-identical regeneration, and a
signature is bytes. RSA PKCS#1 v1.5 is deterministic -- unlike PSS, it has no salt -- so the same key
over the same digest gives the same signature. The key comes from a SHA-256 counter-mode stream over
the seed, primes are found by stepping from a derived candidate, Miller-Rabin's witnesses come from
the same stream, and `signingTime` is a fixed instant. No `random`, no clock.

WHAT THIS CANNOT TELL ANYONE. One signer, RSA-2048, PKCS#1 v1.5, SHA-256, a self-signed certificate,
no timestamp, no revocation data, no PAdES profile. A signature from another producer may differ in
exactly the way that matters, and A4's verdict is bounded to "over RawPage's generated signatures"
for that reason (`Stage3-Mutation-Testing-Guide.md` section 1).

THIS IS NOT A SECURITY COMPONENT. It generates test data. Stage 11's signing links OpenSSL through
the product and shares no code with this file -- and this file shares none with OpenSSL, which is
what lets OpenSSL judge its output.
"""

import hashlib

# The seed every key in the corpus is derived from. Public, fixed, and deliberately named as what
# it is: changing it regenerates every signed file.
SEED = b"RawPage corpus signing identity -- test data, not a secret, Stage 3 T1"

PUBLIC_EXPONENT = 65537

# A fixed signing instant, the same one `pdfbuild.FIXED_DATE` names (2026-08-31), in the UTCTime
# form CMS and X.509 use.
SIGNING_TIME = b"260831000000Z"
NOT_BEFORE = b"260101000000Z"
NOT_AFTER = b"361231235959Z"

TEST_IDENTITY = "RawPage corpus test identity - NOT a real signer"
TEST_ORGANIZATION = "RawPage corpus (generated test data)"


# ---------------------------------------------------------------------------------------------
# DER
# ---------------------------------------------------------------------------------------------

def tlv(tag, content):
    """One DER element. Definite lengths only, in the shortest form -- DER forbids anything else,
    and a verifier that re-encodes signed attributes to check them would disagree with a longer
    form even where BER would accept it."""
    length = len(content)
    if length < 0x80:
        header = bytes([length])
    else:
        encoded = length.to_bytes((length.bit_length() + 7) // 8, "big")
        header = bytes([0x80 | len(encoded)]) + encoded
    return bytes([tag]) + header + content


def sequence(*items):
    return tlv(0x30, b"".join(items))


def set_of(*items):
    """A DER SET OF: its elements sorted by their encodings (X.690 11.6). The signed attributes are
    hashed in exactly this form, so an unsorted set is a signature over bytes no verifier rebuilds."""
    return tlv(0x31, b"".join(sorted(items)))


def integer(value):
    if value == 0:
        return tlv(0x02, b"\x00")
    if value < 0:
        raise ValueError("negative INTEGER is never needed by this encoder")
    body = value.to_bytes((value.bit_length() + 8) // 8, "big")   # room for a clear sign bit
    if len(body) > 1 and body[0] == 0 and body[1] < 0x80:
        body = body[1:]
    return tlv(0x02, body)


def oid(dotted):
    parts = [int(part) for part in dotted.split(".")]
    body = bytearray([40 * parts[0] + parts[1]])
    for part in parts[2:]:
        chunk = [part & 0x7F]
        part >>= 7
        while part:
            chunk.append(0x80 | (part & 0x7F))
            part >>= 7
        body += bytes(reversed(chunk))
    return tlv(0x06, bytes(body))


def null():
    return b"\x05\x00"


def octet_string(data):
    return tlv(0x04, data)


def bit_string(data):
    return tlv(0x03, b"\x00" + data)


def utf8(text):
    return tlv(0x0C, text.encode("utf-8"))


def utc_time(stamp):
    return tlv(0x17, stamp)


def explicit(number, content):
    return tlv(0xA0 | number, content)


def implicit_constructed(number, content):
    return tlv(0xA0 | number, content)


def element_length(data, offset=0):
    """The total length of the DER element starting at `offset` -- header plus content. Used by the
    self-check to prove the output is one well-formed element with nothing trailing."""
    if offset + 2 > len(data):
        raise ValueError("truncated DER header")
    first = data[offset + 1]
    if first < 0x80:
        return 2 + first
    count = first & 0x7F
    if count == 0 or offset + 2 + count > len(data):
        raise ValueError("indefinite or truncated DER length")
    return 2 + count + int.from_bytes(data[offset + 2:offset + 2 + count], "big")


OID_RSA_ENCRYPTION = "1.2.840.113549.1.1.1"
OID_SHA256_WITH_RSA = "1.2.840.113549.1.1.11"
OID_SHA256 = "2.16.840.1.101.3.4.2.1"
OID_DATA = "1.2.840.113549.1.7.1"
OID_SIGNED_DATA = "1.2.840.113549.1.7.2"
OID_CONTENT_TYPE = "1.2.840.113549.1.9.3"
OID_MESSAGE_DIGEST = "1.2.840.113549.1.9.4"
OID_SIGNING_TIME = "1.2.840.113549.1.9.5"
OID_COMMON_NAME = "2.5.4.3"
OID_ORGANIZATION = "2.5.4.10"
OID_BASIC_CONSTRAINTS = "2.5.29.19"
OID_KEY_USAGE = "2.5.29.15"
OID_SUBJECT_KEY_ID = "2.5.29.14"

# SHA-256's DigestInfo prefix (RFC 8017 section 9.2, note 1).
_SHA256_DIGEST_INFO = bytes.fromhex("3031300d060960864801650304020105000420")


# ---------------------------------------------------------------------------------------------
# A deterministic RSA key
# ---------------------------------------------------------------------------------------------

class _Stream:
    """SHA-256 in counter mode over a seed. Not a DRBG anyone should rely on; a reproducible source
    of candidate bits, which is the only property a fixture needs."""

    def __init__(self, seed, label):
        self._prefix = hashlib.sha256(seed + b"\x00" + label).digest()
        self._counter = 0

    def bits(self, count):
        needed = (count + 7) // 8
        out = bytearray()
        while len(out) < needed:
            out += hashlib.sha256(self._prefix + self._counter.to_bytes(8, "big")).digest()
            self._counter += 1
        return int.from_bytes(bytes(out[:needed]), "big") >> (needed * 8 - count)


_SMALL_PRIMES = [p for p in range(3, 2000) if all(p % q for q in range(2, int(p ** 0.5) + 1))]


def _probably_prime(candidate, stream, rounds=40):
    for small in _SMALL_PRIMES:
        if candidate % small == 0:
            return candidate == small
    d = candidate - 1
    shift = 0
    while d % 2 == 0:
        d //= 2
        shift += 1
    for _ in range(rounds):
        witness = 2 + stream.bits(candidate.bit_length()) % (candidate - 3)
        x = pow(witness, d, candidate)
        if x in (1, candidate - 1):
            continue
        for _ in range(shift - 1):
            x = pow(x, 2, candidate)
            if x == candidate - 1:
                break
        else:
            return False
    return True


def _prime(stream, bits):
    # The top TWO bits set, so the product of two such primes is a full 2048 bits rather than
    # sometimes 2047 -- a modulus one bit short is a key some verifiers reject as the wrong size.
    candidate = stream.bits(bits) | (0b11 << (bits - 2)) | 1
    while True:
        if (candidate - 1) % PUBLIC_EXPONENT != 0 and _probably_prime(candidate, stream):
            return candidate
        candidate += 2


class RsaKey:
    """An RSA key derived from a seed and a label. Two labels give two unrelated keys."""

    def __init__(self, label, bits=2048, seed=SEED):
        stream = _Stream(seed, label)
        while True:
            p = _prime(stream, bits // 2)
            q = _prime(stream, bits // 2)
            if p == q:
                continue
            n = p * q
            if n.bit_length() == bits:
                break
        self.n = n
        self.e = PUBLIC_EXPONENT
        self.d = pow(PUBLIC_EXPONENT, -1, (p - 1) * (q - 1))
        self.size = (bits + 7) // 8

    def public_key_der(self):
        """SubjectPublicKeyInfo."""
        return sequence(sequence(oid(OID_RSA_ENCRYPTION), null()),
                        bit_string(sequence(integer(self.n), integer(self.e))))

    def sign_sha256(self, message):
        """RSASSA-PKCS1-v1_5 with SHA-256 (RFC 8017 section 8.2.1)."""
        digest_info = _SHA256_DIGEST_INFO + hashlib.sha256(message).digest()
        padding = b"\xff" * (self.size - 3 - len(digest_info))
        encoded = b"\x00\x01" + padding + b"\x00" + digest_info
        return pow(int.from_bytes(encoded, "big"), self.d, self.n).to_bytes(self.size, "big")

    def verify_sha256(self, message, signature):
        """The public operation, for the self-check. The same arithmetic a verifier does, and the
        weaker half of the check -- `cryptography`, when importable, is the independent one."""
        recovered = pow(int.from_bytes(signature, "big"), self.e, self.n).to_bytes(self.size, "big")
        digest_info = _SHA256_DIGEST_INFO + hashlib.sha256(message).digest()
        return recovered.endswith(b"\x00" + digest_info) and recovered.startswith(b"\x00\x01\xff")


# ---------------------------------------------------------------------------------------------
# The certificate
# ---------------------------------------------------------------------------------------------

def _name(common_name, organization):
    return sequence(
        set_of(sequence(oid(OID_ORGANIZATION), utf8(organization))),
        set_of(sequence(oid(OID_COMMON_NAME), utf8(common_name))))


class Identity:
    """A key and the self-signed certificate that carries its public half."""

    def __init__(self, label, common_name=TEST_IDENTITY, organization=TEST_ORGANIZATION):
        self.key = RsaKey(label)
        self.name = _name(common_name, organization)
        # Positive, eight bytes, derived: a serial number has to be unique per issuer and this
        # issuer only ever issues to itself.
        self.serial = int.from_bytes(hashlib.sha256(b"serial" + label).digest()[:8], "big") >> 1
        self.certificate = self._certificate()

    def _certificate(self):
        spki = self.key.public_key_der()
        # The key identifier is SHA-1 over the public key's BIT STRING contents -- RFC 5280's
        # method (1). SHA-1 is an identifier here, not a security property.
        public_bits = sequence(integer(self.key.n), integer(self.key.e))
        extensions = sequence(
            sequence(oid(OID_BASIC_CONSTRAINTS), tlv(0x01, b"\xff"), octet_string(sequence())),
            sequence(oid(OID_KEY_USAGE), tlv(0x01, b"\xff"),
                     octet_string(tlv(0x03, b"\x06\xc0"))),        # digitalSignature, nonRepudiation
            sequence(oid(OID_SUBJECT_KEY_ID),
                     octet_string(octet_string(hashlib.sha1(public_bits).digest()))))
        algorithm = sequence(oid(OID_SHA256_WITH_RSA), null())
        tbs = sequence(
            explicit(0, integer(2)),                                 # v3
            integer(self.serial),
            algorithm,
            self.name,                                               # issuer: itself
            sequence(utc_time(NOT_BEFORE), utc_time(NOT_AFTER)),
            self.name,                                               # subject
            spki,
            explicit(3, extensions))
        return sequence(tbs, algorithm, bit_string(self.key.sign_sha256(tbs)))


# ---------------------------------------------------------------------------------------------
# CMS SignedData
# ---------------------------------------------------------------------------------------------

def signed_attributes(content_digest):
    """The SET OF Attribute that is actually signed, in its universal-tag form."""
    return set_of(
        sequence(oid(OID_CONTENT_TYPE), set_of(oid(OID_DATA))),
        sequence(oid(OID_SIGNING_TIME), set_of(utc_time(SIGNING_TIME))),
        sequence(oid(OID_MESSAGE_DIGEST), set_of(octet_string(content_digest))))


def detached_signature(identity, content):
    """A DER ContentInfo holding a detached SignedData over `content`.

    The signature is over the DER of the signed attributes with the SET tag (0x31), NOT over the
    `[0] IMPLICIT` form they are stored in (0xA0) -- RFC 5652 section 5.4. Getting that one byte
    wrong gives a signature every verifier rejects, which is exactly what the self-check and the
    gate's real check are for.
    """
    attributes = signed_attributes(hashlib.sha256(content).digest())
    signature = identity.key.sign_sha256(attributes)

    sha256 = sequence(oid(OID_SHA256))
    signer_info = sequence(
        integer(1),
        sequence(identity.name, integer(identity.serial)),          # IssuerAndSerialNumber
        sha256,
        implicit_constructed(0, attributes[_content_offset(attributes):]),
        sequence(oid(OID_RSA_ENCRYPTION), null()),
        octet_string(signature))
    signed_data = sequence(
        integer(1),
        set_of(sha256),
        sequence(oid(OID_DATA)),                                    # no eContent: detached
        implicit_constructed(0, identity.certificate),
        set_of(signer_info))
    return sequence(oid(OID_SIGNED_DATA), explicit(0, signed_data))


def _content_offset(element):
    """Where the content of a DER element starts, past its tag and length."""
    first = element[1]
    return 2 if first < 0x80 else 2 + (first & 0x7F)


def self_check(identity, content, der):
    """Checks one signature before it reaches a corpus file. Returns a one-line description.

    Two halves, as `cryptenc.py`'s AES check has: the arithmetic this file does, re-done in the
    verify direction; and `cryptography`, when importable, parsing the certificate and verifying
    both signatures with code that shares nothing with this module. `cryptography` is NOT a
    dependency of this project, and when it is absent the message says so rather than passing
    quietly. The authoritative check is neither -- it is `openssl cms -verify` in
    `gates.corpus.signed`, with its flipped-byte control run first.
    """
    if element_length(der) != len(der):
        raise AssertionError("cms self-check failed: trailing bytes after the ContentInfo")
    attributes = signed_attributes(hashlib.sha256(content).digest())
    signature = identity.key.sign_sha256(attributes)
    if not identity.key.verify_sha256(attributes, signature):
        raise AssertionError("cms self-check failed: the RSA public operation disagrees")
    if signature not in der or identity.certificate not in der:
        raise AssertionError("cms self-check failed: signature or certificate not in the output")

    try:
        from cryptography import x509
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import padding
    except ImportError:
        return "cms self-check ok (own arithmetic; `cryptography` not importable)"

    certificate = x509.load_der_x509_certificate(identity.certificate)
    public_key = certificate.public_key()
    public_key.verify(certificate.signature, certificate.tbs_certificate_bytes,
                      padding.PKCS1v15(), hashes.SHA256())
    public_key.verify(signature, attributes, padding.PKCS1v15(), hashes.SHA256())
    if certificate.public_key().public_numbers().n != identity.key.n:
        raise AssertionError("cms self-check failed: certificate carries a different modulus")
    return "cms self-check ok (own arithmetic and `cryptography` agree)"


if __name__ == "__main__":
    probe = Identity(b"self-test")
    payload = b"RawPage corpus signing self-check payload." * 7
    blob = detached_signature(probe, payload)
    print("%d-byte ContentInfo, %d-byte certificate" % (len(blob), len(probe.certificate)))
    print(self_check(probe, payload, blob))
