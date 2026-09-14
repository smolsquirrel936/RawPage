#!/usr/bin/env python3
"""Generates `corpus/signed/` -- four genuinely signed documents. Stage 3 T1, decision D3-2.

WHAT THEY ARE FOR. Risk A4 (`Tech-Stack-Plan.md` section 3.3): *does an incremental save preserve
the original bytes closely enough that a pre-existing signature still validates?* Stage 3 T2 writes
a rotation into each of these with `pdf/cos/IncrementalWriter` alone, T13 does it again through the
product's own Save, and both times the result is judged by `openssl cms -verify` (integrity) and by
Acrobat (policy). A signature a verifier has never been seen to reject proves nothing, so
`gates.corpus.signed` flips one byte inside each signed range FIRST and requires a rejection before
it lets the real check count (guide case A-15).

THE FOUR FILES, and what distinguishes them:

    approval.pdf                one approval signature over a classic cross-reference table
    approval-xrefstream.pdf     the same over an xref STREAM with object streams -- the signature
                                dictionary is kept out of the object stream (see below)
    certified-nochanges.pdf     a CERTIFICATION signature with /DocMDP /P 1, "no changes permitted".
                                The control: a policy validator that says nothing after a rotation
                                is saved into THIS file was not judging policy
    approval-then-update.pdf    an approval signature and then an unsigned incremental update, so
                                a save appends a THIRD revision and the signed range ends at the
                                first %%EOF rather than the last

HOW A PDF SIGNATURE IS BUILT, in the order this file does it. The signature dictionary carries a
`/ByteRange [0 a b c]` and a `/Contents <...>` hex string, and the signed bytes are the whole file
EXCEPT that hex string: `[0, a)` and `[b, b + c)`. So the file is serialized first with a
fixed-width `/ByteRange` placeholder and a zero-filled `/Contents` of fixed size; the four numbers
are written over the placeholder without moving a byte; the digest is taken over the two ranges; and
the DER signature is written into `/Contents`, zero-padded to the same length. Every offset computed
before signing is still true after it -- which is the only reason the scheme works at all, and the
same property A4 asks of our writer.

WHY THE SIGNATURE DICTIONARY IS NEVER IN AN OBJECT STREAM. `/ByteRange` names byte offsets in the
FILE. A compressed object stream has no such offsets for what it holds, so a packed signature
dictionary is a signature whose excluded range is nowhere. `pdfbuild.Pdf.serialize(keep_loose=...)`
exists for this one reason.

THE KEY IS DERIVED, NEVER STORED -- `cmsenc.py` says why. One identity signs all four files, named in
its certificate as a corpus test identity. `generate()` asserts the private exponent appears in no
output, which is the committed-key check done where the key is known; guide case A-16 repeats it on
the files, where it is not.

    python corpus/make-signed.py
"""

import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import cmsenc                                                         # noqa: E402
import pdfbuild as pb                                                 # noqa: E402

FILES = {}

# The room reserved for the DER signature. The ContentInfo is about 1.5 KB with its certificate;
# 8 KB is what real producers commonly reserve, and a smaller figure would make a fixture whose
# /Contents is unusually tight -- a shape nothing in the wild has.
CONTENTS_BYTES = 8192

BYTERANGE_PLACEHOLDER = b"[0 0000000000 0000000000 0000000000]"
CONTENTS_PLACEHOLDER = b"<" + b"0" * (2 * CONTENTS_BYTES) + b">"


def generator(name):
    def register(function):
        FILES[name] = function
        return function
    return register


def _signature_dictionary(reason, certification):
    pairs = [(b"/Type", b"/Sig"),
             (b"/Filter", b"/Adobe.PPKLite"),
             (b"/SubFilter", b"/adbe.pkcs7.detached"),
             (b"/Name", pb.literal(cmsenc.TEST_IDENTITY)),
             (b"/Reason", pb.literal(reason)),
             (b"/M", pb.literal(pb.FIXED_DATE))]
    if certification:
        # ISO 32000-1 section 12.8.2.2: a certification signature names the DocMDP transform, and
        # /P 1 is "no changes to the document are permitted; any change invalidates the signature".
        transform = pb.dictionary([(b"/Type", b"/TransformParams"), (b"/P", b"1"),
                                   (b"/V", b"/1.2")])
        pairs.append((b"/Reference", pb.array([pb.dictionary([
            (b"/Type", b"/SigRef"), (b"/TransformMethod", b"/DocMDP"),
            (b"/TransformParams", transform)])])))
    # /ByteRange and /Contents last, so everything a person reads in the dictionary comes first.
    pairs.append((b"/ByteRange", BYTERANGE_PLACEHOLDER))
    pairs.append((b"/Contents", CONTENTS_PLACEHOLDER))
    return pb.dictionary(pairs)


def _only(data, needle):
    at = data.find(needle)
    if at < 0 or data.find(needle, at + 1) >= 0:
        raise AssertionError("expected exactly one %r placeholder" % needle[:24])
    return at


def sign(data, identity):
    """Fill the placeholders of an already-serialized file. Returns the signed bytes."""
    contents_at = _only(data, b"/Contents " + CONTENTS_PLACEHOLDER) + len(b"/Contents ")
    byterange_at = _only(data, b"/ByteRange " + BYTERANGE_PLACEHOLDER) + len(b"/ByteRange ")

    first_length = contents_at                            # [0, a): up to and excluding '<'
    second_start = contents_at + len(CONTENTS_PLACEHOLDER)  # [b, EOF): from just past '>'
    second_length = len(data) - second_start
    numbers = b"[0 %d %d %d" % (first_length, second_start, second_length)
    filled = numbers + b" " * (len(BYTERANGE_PLACEHOLDER) - len(numbers) - 1) + b"]"
    if len(filled) != len(BYTERANGE_PLACEHOLDER):
        raise AssertionError("the /ByteRange placeholder is too narrow for this file")
    data = data[:byterange_at] + filled + data[byterange_at + len(filled):]

    signed = data[:first_length] + data[second_start:]
    der = cmsenc.detached_signature(identity, signed)
    cmsenc.self_check(identity, signed, der)
    if len(der) > CONTENTS_BYTES:
        raise AssertionError("signature of %d bytes does not fit /Contents" % len(der))
    body = der.hex().upper().encode("ascii").ljust(2 * CONTENTS_BYTES, b"0")
    return data[:contents_at + 1] + body + data[second_start - 1:]


def _document(identity, filename, lines, reason, certification=False, objstm=False):
    """One signed single-page document. Returns (bytes, catalog, info, object count)."""
    pdf = pb.Pdf()
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    resources = pb.dictionary([(b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)),
                                                         (b"/F2", pb.ref(bold))]))])
    field = pdf.reserve()
    signature = pdf.reserve()

    text = [pb.show_text(48, 780, 14, "signed/" + filename, b"/F2"),
            pb.show_text(48, 752, 10, "Signed by a GENERATED corpus test identity. "
                                      "This is not a real signature."),
            pb.show_text(48, 736, 10, "Key derived from a public seed in corpus/cmsenc.py; "
                                      "no private key is committed.")]
    text += [pb.show_text(48, 708 - index * 14, 10, line) for index, line in enumerate(lines)]
    top = 708 - len(lines) * 14 - 20
    text += [pb.show_text(48, top - index * 11.6, 9, line)
             for index, line in enumerate(pb.text_body(700 + len(filename), 40))]
    contents = pb.content_stream(pdf, b"\n".join(text) + b"\n")

    root_extra = [(b"/AcroForm", pb.dictionary([(b"/Fields", pb.array([pb.ref(field)])),
                                                 (b"/SigFlags", b"3")]))]
    if certification:
        root_extra.append((b"/Perms", pb.dictionary([(b"/DocMDP", pb.ref(signature))])))
    catalog, _, pages = pb.simple_document(
        pdf, [lambda parent: pb.page(parent, pb.A4, contents, resources,
                                     extra=[(b"/Annots", pb.array([pb.ref(field)]))])],
        root_extra=root_extra)

    # An invisible signature: a merged field and widget with an empty /Rect, /F 132 (Print, Locked).
    pdf.set(field, pb.dictionary([
        (b"/Type", b"/Annot"), (b"/Subtype", b"/Widget"), (b"/FT", b"/Sig"),
        (b"/T", pb.literal("Signature1")), (b"/V", pb.ref(signature)),
        (b"/F", b"132"), (b"/Rect", pb.rect(0, 0, 0, 0)), (b"/P", pb.ref(pages[0]))]))
    pdf.set(signature, _signature_dictionary(reason, certification))

    info = pb.info_object(pdf, "RawPage corpus: signed/" + filename)
    data = pdf.serialize(catalog, info, objstm=objstm,
                         keep_loose=[signature] if objstm else ())
    return sign(data, identity), catalog, info, pdf.count()


@generator("approval.pdf")
def approval(identity):
    """An approval signature over a classic cross-reference table: the plain case."""
    return _document(identity, "approval.pdf", [
        "One APPROVAL signature. Classic cross-reference table, one revision.",
        "A4: after RawPage saves a rotation here, OpenSSL and Acrobat must both call it valid."],
        "RawPage corpus: approval signature")[0]


@generator("approval-xrefstream.pdf")
def approval_xrefstream(identity):
    """The same signature over a cross-reference STREAM, with the page and its furniture packed
    into an object stream. The page dictionary a rotation changes is therefore a type-2 object,
    which is the D3-5 case where the writer must emit it as an ordinary object instead."""
    return _document(identity, "approval-xrefstream.pdf", [
        "One APPROVAL signature. Cross-reference STREAM; the page lives in an /ObjStm.",
        "The signature dictionary itself is an ordinary object -- /ByteRange needs its offsets."],
        "RawPage corpus: approval signature, xref stream", objstm=True)[0]


@generator("certified-nochanges.pdf")
def certified_nochanges(identity):
    """A certification signature with /DocMDP /P 1. Stage 3 saves a rotation into it without
    honouring the policy -- that is Stage 11's -- so a policy validator MUST flag the result. If it
    does not, its verdict on the three approval files was not a judgement (guide M-03)."""
    return _document(identity, "certified-nochanges.pdf", [
        "A CERTIFICATION signature, /DocMDP /P 1: no changes are permitted.",
        "The CONTROL: after a saved rotation, a policy validator must report it as broken."],
        "RawPage corpus: certification, no changes permitted", certification=True)[0]


@generator("approval-then-update.pdf")
def approval_then_update(identity):
    """An approval signature, then an unsigned incremental update that replaces /Info. A save
    appends a third revision, and the signed range ends at the FIRST %%EOF -- so a writer that
    measured "the original bytes" from the newest revision rather than the start of the file, or a
    validator that assumed a signature covers the whole file, is caught here and nowhere else."""
    signed, catalog, info, size = _document(identity, "approval-then-update.pdf", [
        "An APPROVAL signature, then an UNSIGNED incremental update replacing /Info.",
        "The signed range ends at the first %%EOF; a RawPage save appends the THIRD revision."],
        "RawPage corpus: approval signature before an update")
    updated_info = pb.dictionary([
        (b"/Title", pb.utf16("RawPage corpus: signed/approval-then-update.pdf (updated)")),
        (b"/Producer", pb.literal(pb.PRODUCER)),
        (b"/CreationDate", pb.literal(pb.FIXED_DATE)),
        (b"/ModDate", pb.literal(pb.FIXED_DATE))])
    updated = pb.incremental_update(signed, [(info, updated_info)], root=catalog, info=info,
                                    size=size)

    # `incremental_update` hashes a fresh /ID for both elements. ISO 32000-1 section 14.4 keeps the
    # first element permanent across revisions, so it is put back -- same width, so no offset moves.
    identifier = re.compile(rb"/ID \[ <([0-9A-F]{32})> <([0-9A-F]{32})> \]")
    permanent = identifier.findall(signed)[-1][0]
    last = list(identifier.finditer(updated))[-1]
    if last.start() < len(signed):
        raise AssertionError("the update carries no /ID of its own")
    return updated[:last.start(1)] + permanent + updated[last.end(1):]


def generate():
    identity = cmsenc.Identity(b"corpus/signed")
    files = {"signed/" + name: builder(identity) for name, builder in FILES.items()}

    # The committed-key check, done where the key is known. A-16 repeats the structural half of it
    # over the files on disk, where it is not.
    exponent = identity.key.d.to_bytes(identity.key.size, "big")
    for relative, data in files.items():
        for form in (exponent, exponent.hex().encode("ascii"), exponent.hex().upper().encode("ascii")):
            if form in data:
                raise AssertionError("%s contains the private exponent" % relative)
    return files


if __name__ == "__main__":
    root = pathlib.Path(__file__).resolve().parent
    (root / "signed").mkdir(exist_ok=True)
    for relative, data in generate().items():
        (root / relative).write_bytes(data)
        print("wrote %-34s %8d bytes" % (relative, len(data)))
