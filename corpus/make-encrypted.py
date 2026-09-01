#!/usr/bin/env python3
"""Generates `corpus/encrypted/` -- four genuinely encrypted documents.

WHY THEY ARE REAL RATHER THAN DECORATIVE. D2-6 (`Stage2-Rendering-Plan.md` section 3) splits
Stage 2's encryption story in two: PDFium opens these through a password callback, and the
first-party COS reader DECLINES them by name until `StandardSecurityHandler` arrives at Stage 10.
Both halves are only testable against a file whose bytes are actually enciphered. A file carrying
an /Encrypt dictionary over plaintext would let the COS reader's decline look correct while PDFium
silently failed, and those two results are indistinguishable from the pair we want.

THE PASSWORDS ARE FIXED AND PUBLIC, and they are in `corpus/PROVENANCE.md`:

    user password   "user"          opens the document
    owner password  "owner"         opens it with full permissions

There is nothing to protect here -- these are fixtures -- and a fixture whose password is a secret
is a fixture nobody can use. `owner-empty-user.pdf` is the exception worth knowing about: its USER
password is empty, so it opens with no prompt at all while still being encrypted, which is the
commonest real-world arrangement and the one a reader most often mistakes for an unencrypted file.

WHAT IS NOT HERE. No signed document -- signing is Stage 10 and risk A4 belongs to Stage 3's
round-trip suite, so a `corpus/signed-*.pdf` produced now would be a fixture with nothing to check
it. No public-key (/Filter /Adobe.PubSec) encryption: it needs a certificate, a certificate needs
a private key, and a private key committed to a repository is a bad habit even when it protects
nothing.

    python corpus/make-encrypted.py
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import cryptenc                                                       # noqa: E402
import pdfbuild as pb                                                 # noqa: E402

FILES = {}

USER_PASSWORD = "user"
OWNER_PASSWORD = "owner"

# A fixed /ID first element. Algorithm 2 mixes it into the file key, so it must be stable for the
# corpus to regenerate identically -- and it must be the value the trailer actually carries, which
# is why `_build` writes the trailer's /ID by hand instead of letting `Pdf` hash the body.
FILE_ID = bytes((index * 11 + 3) & 0xFF for index in range(16))


def generator(name):
    def register(function):
        FILES[name] = function
        return function
    return register


def _build(revision, title, note, user=USER_PASSWORD, owner=OWNER_PASSWORD, permissions=-4):
    """One encrypted document: two pages, every stream and every string enciphered.

    The /Encrypt dictionary itself is NOT encrypted, and neither is the trailer's /ID -- both are
    exceptions ISO 32000-1 section 7.6.1 names, and a generator that missed either would produce a
    file no reader on earth can open.
    """
    encryptor = cryptenc.Encryptor(revision, user=user, owner=owner,
                                   permissions=permissions, file_id=FILE_ID)

    pdf = pb.Pdf(version="1.7" if revision < 5 else "2.0")
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    resources = pb.dictionary([(b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)),
                                                         (b"/F2", pb.ref(bold))]))])
    builders = []
    for index in range(2):
        body = b"\n".join([
            pb.show_text(48, 780, 14, title, b"/F2"),
            pb.show_text(48, 745, 10, note),
            pb.show_text(48, 725, 10, 'user password "user", owner password "owner"'),
            pb.show_text(48, 700, 10, "page %d of 2" % (index + 1)),
        ] + [pb.show_text(48, 660 - line * 11.6, 9, text)
             for line, text in enumerate(pb.text_body(400 + index, 24))]) + b"\n"
        contents = pb.content_stream(pdf, body)
        builders.append(lambda parent, c=contents: pb.page(parent, pb.A4, c, resources))
    catalog, _, _ = pb.simple_document(pdf, builders)

    # The /Info strings are encrypted by hand: `Pdf` enciphers stream data automatically but a
    # string is just bytes inside an already-assembled dictionary, and a builder that went looking
    # for string literals in those bytes would be guessing (see `cryptenc.Encryptor`).
    info_number = pdf.reserve()

    def encrypted_text(text):
        raw = b"\xfe\xff" + text.encode("utf-16-be")
        return pb.hexstring(encryptor.encrypt_string(info_number, 0, raw))

    pdf.set(info_number, pb.dictionary([
        (b"/Title", encrypted_text(title)),
        (b"/Producer", encrypted_text(pb.PRODUCER.decode("ascii"))),
        (b"/CreationDate", pb.hexstring(
            encryptor.encrypt_string(info_number, 0, pb.FIXED_DATE))),
        (b"/ModDate", pb.hexstring(
            encryptor.encrypt_string(info_number, 0, pb.FIXED_DATE)))]))

    encrypt_number = pdf.add(pb.dictionary(encryptor.dictionary_pairs()))
    pdf.encryptor = encryptor.encrypt_stream

    data = pdf.serialize(catalog, info_number, extra_trailer=[
        (b"/Encrypt", pb.ref(encrypt_number))])

    # Replace the content-derived /ID with the one the key was computed from. It has to be the
    # LAST /ID in the file (the trailer's), and it has to be both elements.
    head, _, tail = data.rpartition(b"/ID ")
    _, _, rest = tail.partition(b"] ")
    identifier = pb.array([pb.hexstring(FILE_ID), pb.hexstring(FILE_ID)])
    return head + b"/ID " + identifier + b" " + rest


@generator("rc4-40.pdf")
def rc4_40():
    """R2 / V1: RC4 with a 40-bit key. The original standard security handler, still emitted by
    old tooling and by anything that predates Acrobat 5."""
    return _build(2, "encrypted/rc4-40.pdf", "RC4, 40-bit key, revision 2")


@generator("rc4-128.pdf")
def rc4_128():
    """R3 / V2: RC4 with a 128-bit key, and the 50-round key derivation that came with it."""
    return _build(3, "encrypted/rc4-128.pdf", "RC4, 128-bit key, revision 3")


@generator("aes-128.pdf")
def aes_128():
    """R4 / V4: AES-128 through a named crypt filter (/StdCF, /CFM /AESV2).

    Revision 4 is where crypt FILTERS appear, so streams and strings can in principle use
    different algorithms. This file uses /StdCF for both, which is what every real producer does --
    the case where they differ belongs with Stage 10's handler and its own fixtures."""
    return _build(4, "encrypted/aes-128.pdf", "AES-128 (AESV2), revision 4, crypt filters")


@generator("aes-256.pdf")
def aes_256():
    """R6 / V5: AES-256 (AESV3), the ISO 32000-2 handler.

    The only one of the four not built on MD5: the password check is the iterated SHA-2/AES hash
    of Algorithm 2.B, the file key is wrapped in /UE and /OE rather than derived from the password,
    and there is no per-object key derivation at all. A reader that handles R2 through R4 shares
    almost no code with one that handles this."""
    return _build(6, "encrypted/aes-256.pdf", "AES-256 (AESV3), revision 6")


@generator("owner-empty-user.pdf")
def owner_empty_user():
    """AES-128 with an EMPTY user password and an owner password set, and printing denied.

    It opens with no prompt, which is what makes it worth having: a reader that treats "no password
    needed" as "not encrypted" gets every byte of this document wrong, and nothing on screen says
    so. The permission bits are also the only place in the corpus where they are not the default --
    they are a Stage 10 concern (`core/doc/Permissions`), recorded here so the fixture exists
    before the code that reads it."""
    # -3904: printing (bit 3) and high-quality printing (bit 12) cleared, the rest allowed.
    return _build(4, "encrypted/owner-empty-user.pdf",
                  "AES-128, empty user password, printing denied",
                  user="", permissions=-3904)


def generate():
    return {"encrypted/" + name: builder() for name, builder in FILES.items()}


if __name__ == "__main__":
    root = pathlib.Path(__file__).resolve().parent
    (root / "encrypted").mkdir(exist_ok=True)
    for relative, data in generate().items():
        target = root / relative
        target.write_bytes(data)
        print("wrote %-34s %8d bytes" % (relative, len(data)))
