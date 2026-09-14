#!/usr/bin/env python3
"""Judges PDF signatures with OpenSSL, reading the file as BYTES. Stage 3 T1, decision D3-2.

    check-signatures.py --openssl <exe> FILE...      guide case A-15 (and A-28's procedure at T2)
    check-signatures.py --shapes FILE...             guide case A-16, over corpus/signed/
    check-signatures.py --shapes-self-test DIR       A-16's detections, each driven to fire

WHAT IT CHECKS, AND IN WHAT ORDER. For every signature in every file:

  1. **The controls, first.** One byte is flipped inside the first signed range and, separately,
     one inside the second; each damaged copy is re-read from scratch and handed to the validator,
     which must REJECT it. A validator that accepts a flipped byte is not validating anything, so
     if any control verifies the run STOPS there and the real check never counts. That is
     Stage 2 I-41's lesson -- an instrument is shown a known answer before its reading means
     anything -- applied to the instrument A4's verdict rests on.
  2. **The real check.** The unmodified file, which must verify.

WHY IT DOES NOT USE `pdf/cos/`. A verifier that finds the signed bytes through the reader the writer
was built beside agrees with the writer (D3-2; the same reason Stage 2's P-4 wanted an independent
renderer). A PDF signature does not need a parser to be checked: `/ByteRange [0 a b c]` names the
signed bytes `[0, a)` and `[b, b + c)` directly, and the byte at `a` is the `<` that opens the
`/Contents` hex string, whose `>` is the byte at `b - 1`. So this reads a regular expression, four
integers and a hex string, and checks those three structural facts before trusting them.

WHAT OPENSSL IS ASKED. `openssl cms -verify -binary -inform DER -noverify`: the digest over the
two ranges matches the signed `messageDigest` attribute, and the RSA signature over the signed
attributes is good. `-noverify` skips the certificate chain, because every certificate in the corpus
is a self-signed test identity. What OpenSSL CANNOT say -- modification policy, `/DocMDP`, what a
later revision did -- is Acrobat's half of the verdict, guide case M-03.

`--openssl-prefix` inserts arguments before `cms`. It exists for the gate's two controls on THIS
script -- a validator that accepts everything (`cmake -E true`) must stop the run, and one that
rejects everything must fail it -- and has no other use.
"""

import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

BYTERANGE = re.compile(rb"/ByteRange\s*\[\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*\]")

TEST_IDENTITY = b"RawPage corpus test identity"

# Private-key structures that must not be in any committed file, as DER prefixes:
#   PKCS#1 RSAPrivateKey  SEQUENCE { INTEGER 0, INTEGER modulus (257 bytes, leading zero), ... }
#   PKCS#8 PrivateKeyInfo SEQUENCE { INTEGER 0, AlgorithmIdentifier rsaEncryption NULL, OCTET STRING }
PKCS1_PRIVATE = bytes.fromhex("0201000282010100")
PKCS8_PRIVATE = bytes.fromhex("020100300d06092a864886f70d010101050004")


class Failure(Exception):
    pass


# ---------------------------------------------------------------------------------------------
# Extraction
# ---------------------------------------------------------------------------------------------

def _der_length(data):
    """Total length of the DER element at the start of `data`, or 0 if it is not one."""
    if len(data) < 2 or data[0] != 0x30:
        return 0
    first = data[1]
    if first < 0x80:
        return 2 + first
    count = first & 0x7F
    if count == 0 or count > 4 or len(data) < 2 + count:
        return 0
    return 2 + count + int.from_bytes(data[2:2 + count], "big")


def signatures(data, label):
    """Every signature in `data`: a list of dicts with the range, its span, and the DER."""
    found = []
    for match in BYTERANGE.finditer(data):
        start, first, second, tail = (int(group) for group in match.groups())
        where = "%s /ByteRange [%d %d %d %d]" % (label, start, first, second, tail)
        if start != 0:
            raise Failure("%s: the first range does not start at byte 0" % where)
        if not (first < second <= len(data) and second + tail <= len(data)):
            raise Failure("%s: the ranges do not fit a %d-byte file" % (where, len(data)))
        if data[first:first + 1] != b"<" or data[second - 1:second] != b">":
            raise Failure("%s: the gap is not exactly the /Contents hex string" % where)
        hex_digits = data[first + 1:second - 1]
        if not re.fullmatch(rb"[0-9A-Fa-f]*", hex_digits) or len(hex_digits) % 2:
            raise Failure("%s: /Contents is not a hex string" % where)
        blob = bytes.fromhex(hex_digits.decode("ascii"))
        size = _der_length(blob)
        if size == 0 or size > len(blob) or any(blob[size:]):
            raise Failure("%s: /Contents does not hold one DER element and zero padding" % where)
        found.append({
            "label": "%s#%d" % (label, len(found) + 1),
            "index": len(found),
            "ranges": (first, second, tail),
            "span": (match.start(), match.end()),
            "content": data[:first] + data[second:second + tail],
            "der": blob[:size],
            "whole_file": second + tail == len(data),
        })
    return found


# ---------------------------------------------------------------------------------------------
# The validator
# ---------------------------------------------------------------------------------------------

def run_validator(command, der, content, scratch):
    signature = scratch / "signature.der"
    signed = scratch / "signed.bin"
    output = scratch / "output.bin"
    signature.write_bytes(der)
    signed.write_bytes(content)
    completed = subprocess.run(
        command + ["cms", "-verify", "-binary", "-inform", "DER", "-in", str(signature),
                   "-content", str(signed), "-noverify", "-out", str(output)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = completed.stdout.decode("utf-8", "replace").strip().splitlines()
    return completed.returncode == 0, (text[0] if text else "(no output)")


def flip_offsets(entry):
    """One offset in each signed range, clear of the /ByteRange token itself -- flipping one of its
    digits would make the copy fail EXTRACTION, and a control must fail for the reason it names."""
    first, second, tail = entry["ranges"]
    span_start, span_end = entry["span"]
    inside_first = first // 2
    if span_start <= inside_first < span_end:
        inside_first = span_start // 2
    return [("first range", inside_first), ("second range", second + tail // 2)]


def check(arguments):
    command = [arguments.openssl] + list(arguments.openssl_prefix)
    if not arguments.openssl_prefix:
        version = subprocess.run([arguments.openssl, "version"], stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT).stdout.decode("utf-8", "replace")
        print("validator: %s (%s)" % (arguments.openssl, version.strip()))
    else:
        print("validator: %s %s  <- a CONTROL on this script, not a validator"
              % (arguments.openssl, " ".join(arguments.openssl_prefix)))

    files = [pathlib.Path(name) for name in arguments.files]
    entries = []
    for path in files:
        data = path.read_bytes()
        found = signatures(data, path.name)
        if not found:
            raise Failure("%s: no /ByteRange -- no signature to judge" % path.name)
        for entry in found:
            entry["path"] = path
            first, second, tail = entry["ranges"]
            print("signature %s: /ByteRange [0 %d %d %d], %d-byte file, %s, %d-byte CMS"
                  % (entry["label"], first, second, tail, len(data),
                     "covers to end of file" if entry["whole_file"]
                     else "ends at byte %d of %d" % (second + tail, len(data)),
                     len(entry["der"])))
        entries += found

    with tempfile.TemporaryDirectory(prefix="rawpage-signatures-") as directory:
        scratch = pathlib.Path(directory)

        # 1. The controls. Each flipped copy is RE-EXTRACTED from the damaged bytes, so the
        #    control exercises the same path as the real check rather than a buffer built beside it.
        controls = 0
        for entry in entries:
            original = entry["path"].read_bytes()
            for where, offset in flip_offsets(entry):
                damaged = bytearray(original)
                damaged[offset] ^= 0x01
                again = signatures(bytes(damaged), entry["path"].name)[entry["index"]]
                accepted, message = run_validator(command, again["der"], again["content"], scratch)
                print("control   %s flipped byte %d (%s): %s -- %s"
                      % (entry["label"], offset, where,
                         "VERIFIED" if accepted else "rejected", message))
                if accepted:
                    raise Failure(
                        "CONTROL VERIFIED: the validator accepted %s with byte %d flipped. It is "
                        "not checking the signed range, so no signature it reports as valid means "
                        "anything -- the real check is NOT run." % (entry["label"], offset))
                controls += 1
        print("all %d flipped copies rejected" % controls)

        # 2. The real check.
        verified = 0
        for entry in entries:
            accepted, message = run_validator(command, entry["der"], entry["content"], scratch)
            print("verify    %s: %s -- %s"
                  % (entry["label"], "verified" if accepted else "does NOT verify", message))
            verified += accepted
        if verified != len(entries):
            raise Failure("%d of %d signatures do NOT verify" % (len(entries) - verified,
                                                                  len(entries)))

    print("signature check ok: %d of %d controls rejected, %d of %d signatures verify"
          % (controls, controls, verified, len(entries)))


# ---------------------------------------------------------------------------------------------
# A-16: the shapes corpus/signed/ claims
# ---------------------------------------------------------------------------------------------

def _startxref_kind(data):
    at = data.rfind(b"startxref")
    match = re.match(rb"startxref\s+(\d+)", data[at:])
    if at < 0 or not match:
        return "none"
    offset = int(match.group(1))
    if data[offset:offset + 4] == b"xref":
        return "classic"
    if re.match(rb"\d+\s+\d+\s+obj\s*<<(?:(?!>>).)*?/Type\s*/XRef", data[offset:offset + 400],
                re.S):
        return "stream"
    return "unknown"


def shape_problems(name, data):
    problems = []
    found = signatures(data, name)
    eofs = [match.end() for match in re.finditer(rb"%%EOF", data)]
    certified = (re.search(rb"/Perms\s*<<\s*/DocMDP\s+\d+\s+\d+\s+R", data) is not None
                 and re.search(rb"/TransformMethod\s*/DocMDP", data) is not None
                 and re.search(rb"/TransformParams\s*<<(?:(?!>>).)*?/P\s+1(?![0-9])", data, re.S)
                 is not None)

    expected = {
        "approval.pdf":             {"kind": "classic", "revisions": 1, "certified": False},
        "approval-xrefstream.pdf":  {"kind": "stream",  "revisions": 1, "certified": False},
        "certified-nochanges.pdf":  {"kind": "classic", "revisions": 1, "certified": True},
        "approval-then-update.pdf": {"kind": "classic", "revisions": 2, "certified": False},
    }.get(name)
    if expected is None:
        return ["%s: not a corpus/signed/ file this check knows" % name]

    if len(found) != 1:
        problems.append("%s: %d signatures, expected exactly 1" % (name, len(found)))
    if _startxref_kind(data) != expected["kind"]:
        problems.append("%s: the newest cross-reference section is %s, expected %s"
                        % (name, _startxref_kind(data), expected["kind"]))
    if len(eofs) != expected["revisions"]:
        problems.append("%s: %d revisions (%%%%EOF markers), expected %d"
                        % (name, len(eofs), expected["revisions"]))
    if certified != expected["certified"]:
        problems.append("%s: %s" % (name, "no /Perms /DocMDP with /P 1" if expected["certified"]
                                    else "carries a /DocMDP certification it should not"))
    if found and eofs:
        first, second, tail = found[0]["ranges"]
        end = second + tail
        if expected["revisions"] == 1 and end != len(data):
            problems.append("%s: the signed range ends at %d, not at the end of the file" % (name, end))
        if expected["revisions"] > 1 and not (eofs[0] <= end <= eofs[0] + 2 and end < len(data)):
            problems.append("%s: the signed range ends at %d, not at the FIRST %%%%EOF (%d)"
                            % (name, end, eofs[0]))

    # No private key, in the file or in the signature it carries (hex-encoded in the file).
    carried = b"".join(entry["der"] for entry in found)
    for label, haystack in (("the file", data), ("its signature", carried)):
        if b"PRIVATE KEY" in haystack:
            problems.append("%s: a PEM private-key block in %s" % (name, label))
        if PKCS1_PRIVATE in haystack or PKCS8_PRIVATE in haystack:
            problems.append("%s: a DER private-key structure in %s" % (name, label))
    if found and TEST_IDENTITY not in carried:
        problems.append("%s: the certificate does not name itself a corpus test identity" % name)
    return problems


def shapes(arguments):
    problems = []
    for name in arguments.files:
        path = pathlib.Path(name)
        found = shape_problems(path.name, path.read_bytes())
        print("shape     %s: %s" % (path.name, "ok" if not found else "%d problem(s)" % len(found)))
        problems += found
    if problems:
        raise Failure("\n".join(problems))
    print("shape check ok: %d files have the shapes corpus/PROVENANCE.md claims" % len(arguments.files))


def shapes_self_test(arguments):
    """Each A-16 detection driven with damage that must trigger it, in memory (convention 7)."""
    root = pathlib.Path(arguments.shapes_self_test)
    clean = {name: (root / name).read_bytes() for name in
             ("approval.pdf", "approval-xrefstream.pdf", "certified-nochanges.pdf",
              "approval-then-update.pdf")}
    for name, data in clean.items():
        if shape_problems(name, data):
            raise Failure("the self-test needs a clean corpus/signed/: %s is not" % name)

    def swap(data, old, new):
        if data.count(old) < 1:
            raise Failure("self-test fixture changed: %r not found" % old)
        return data.replace(old, new, 1)

    first_eof = clean["approval-then-update.pdf"].find(b"%%EOF") + len(b"%%EOF\n")
    cases = [
        ("a PEM key block", "approval.pdf",
         clean["approval.pdf"] + b"-----BEGIN PRIVATE KEY-----\n", "PEM private-key"),
        ("a PKCS#8 structure", "approval.pdf",
         clean["approval.pdf"] + PKCS8_PRIVATE, "DER private-key"),
        ("the policy weakened to /P 2", "certified-nochanges.pdf",
         swap(clean["certified-nochanges.pdf"], b"/P 1", b"/P 2"), "no /Perms /DocMDP"),
        ("a certification where none belongs", "approval.pdf",
         swap(clean["approval.pdf"], b"/SigFlags 3 >>",
              b"/SigFlags 3 >> /Perms << /DocMDP 4 0 R >> /Reference [ << /TransformMethod "
              b"/DocMDP /TransformParams << /P 1 >> >> ]"), "should not"),
        ("the update stripped off", "approval-then-update.pdf",
         clean["approval-then-update.pdf"][:first_eof], "revisions"),
        ("a classic file claiming to be a stream", "approval-xrefstream.pdf",
         clean["approval.pdf"], "expected stream"),
        ("a certificate with another name", "approval.pdf",
         _rename_identity(clean["approval.pdf"]), "test identity"),
    ]
    fired = 0
    for description, name, data, marker in cases:
        problems = shape_problems(name, data)
        hit = any(marker in problem for problem in problems)
        print("  %-40s %s" % (description, "detected" if hit else "NOT DETECTED"))
        fired += hit
    if fired != len(cases):
        raise Failure("%d of %d shape detections did not fire" % (len(cases) - fired, len(cases)))
    print("shape self-test ok: %d of %d detections fired" % (fired, len(cases)))


def _rename_identity(data):
    """The approval file with the certificate's test-identity text altered inside /Contents -- the
    signature no longer verifies, which does not matter here: only the name check is exercised.

    EVERY occurrence, because the name is in the CMS three times -- the certificate's issuer and
    subject, and the SignerInfo's issuer. The first version of this control changed one of them and
    the detection rightly stayed quiet, which is what running a control is for."""
    needle = TEST_IDENTITY.hex().upper().encode("ascii")
    if needle not in data:
        raise Failure("self-test fixture changed: the identity is not in /Contents")
    return data.replace(needle, b"58" + needle[2:])             # "RawPage" -> "XawPage"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--openssl", help="the validator executable")
    parser.add_argument("--openssl-prefix", action="append", default=[],
                        help="an argument inserted before `cms` (the gate's controls only)")
    parser.add_argument("--shapes", action="store_true", help="guide case A-16 over corpus/signed/")
    parser.add_argument("--shapes-self-test", metavar="DIR",
                        help="drive each A-16 detection to fire, over the files in DIR")
    parser.add_argument("files", nargs="*")
    arguments = parser.parse_args()
    try:
        if arguments.shapes_self_test:
            shapes_self_test(arguments)
        elif arguments.shapes:
            shapes(arguments)
        elif arguments.openssl and arguments.files:
            check(arguments)
        else:
            parser.print_usage()
            return 2
    except Failure as failure:
        print("signature check FAILED: %s" % failure)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
