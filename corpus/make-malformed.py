#!/usr/bin/env python3
"""Generates `corpus/malformed/` -- the damaged files, each damaged in one named way.

WHY GENERATED DAMAGE IS BETTER THAN COLLECTED DAMAGE. D2-11 (`Stage2-Rendering-Plan.md` section 3)
makes the argument in one line: *"the xref offsets are 12 bytes short from object 4 onward"* is a
test, while *"this file is broken somehow"* is a mystery that gets re-diagnosed every time it fails.
Every file here starts from a valid document built by the same code that builds `corpus/ordinary/`,
and then has EXACTLY ONE thing done to it. The damage is in the function name, in the docstring,
and in `corpus/PROVENANCE.md`, so a failure in `XrefRepair` (T8) can be read against what the file
actually is rather than against a guess.

ONE DAMAGE PER FILE is a rule, not a style. A file with two faults cannot tell you which one your
repair path handled, and the exit criterion these exist for -- A-21, *every* file yields a report
and never a crash -- is only meaningful if a report can be checked against a known cause.

WHAT THESE ARE NOT. They are not fuzzer output. `tests/fuzz/` (T5, T6) is seeded from this
directory and will produce far stranger input than anything here; these are the damages a real
producer, a truncated download or a bad byte range actually creates, and they are the ones a repair
path is expected to RECOVER from rather than merely survive.

    python corpus/make-malformed.py
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import pdfbuild as pb                                                 # noqa: E402

FILES = {}

_ORDINARY = None


def _ordinary(name):
    """Build one `corpus/ordinary/` file by calling its generator, so a damaged file and its
    control cannot drift apart. `make-ordinary.py`'s hyphen makes it un-importable by name, which
    is why this goes through `importlib` rather than an `import` statement."""
    global _ORDINARY
    if _ORDINARY is None:
        import importlib.util

        path = pathlib.Path(__file__).resolve().parent / "make-ordinary.py"
        spec = importlib.util.spec_from_file_location("_make_ordinary", path)
        _ORDINARY = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_ORDINARY)
    return _ORDINARY.FILES[name]()


def generator(name):
    def register(function):
        FILES[name] = function
        return function
    return register


# ---------------------------------------------------------------------------------------------
# The healthy document every damaged file starts from
# ---------------------------------------------------------------------------------------------

def _healthy(pages=3, version="1.7", uncompressed=True):
    """A small, valid, deliberately BORING document.

    Boring on purpose: the subject of every file in this directory is the damage, so the content
    must not be able to fail for a second reason. Uncompressed content streams by default, because
    a hex editor is the tool anyone reaching for these files is already holding.
    """
    pdf = pb.Pdf(version=version)
    regular = pb.base14(pdf, "Helvetica")
    bold = pb.base14(pdf, "Helvetica-Bold")
    resources = pb.dictionary([(b"/Font", pb.dictionary([(b"/F1", pb.ref(regular)),
                                                         (b"/F2", pb.ref(bold))]))])
    builders = []
    for index in range(pages):
        body = b"\n".join([
            pb.show_text(48, 780, 14, "malformed corpus base - page %d of %d" % (index + 1, pages),
                         b"/F2"),
            pb.show_text(48, 740, 10, "one document, one deliberate fault; see PROVENANCE.md"),
        ] + [pb.show_text(48, 700 - line * 12, 9, text)
             for line, text in enumerate(pb.text_body(index + 1, 20))]) + b"\n"
        contents = pb.content_stream(pdf, body, compressed=not uncompressed)
        builders.append(lambda parent, c=contents: pb.page(parent, pb.A4, c, resources))
    catalog, pages_number, page_numbers = pb.simple_document(pdf, builders)
    info = pb.info_object(pdf, "RawPage corpus: malformed base document")
    return pdf, catalog, info, pages_number, page_numbers


def _base_bytes(**kwargs):
    pdf, catalog, info, _, _ = _healthy(**kwargs)
    return pdf.serialize(catalog, info)


def _split_xref(data):
    """(everything before `xref`, the xref section, the trailer onward). Used by the offset
    damages, which have to rewrite entries without touching the objects above them."""
    head, _, rest = data.partition(b"\nxref\n")
    body, _, tail = rest.partition(b"trailer")
    return head, body, tail


# ---------------------------------------------------------------------------------------------
# Cross-reference damage
# ---------------------------------------------------------------------------------------------

@generator("xref-offsets-short.pdf")
def xref_offsets_short():
    """Every xref offset from object 4 onward is 12 bytes too small.

    D2-11 names this one explicitly as the shape a described damage should have. It is what a
    producer that edited a file in place and forgot to rewrite the table produces, and it is
    recoverable: the objects are all intact and in order, so a scan finds them.
    """
    data = _base_bytes()
    head, body, tail = _split_xref(data)
    lines = body.split(b"\n")
    out = [lines[0], lines[1]]                      # "0 N" and the free head
    for index, line in enumerate(lines[2:], start=1):
        if len(line) == 19 and line.endswith(b"n ") and index >= 4:
            offset = int(line[:10]) - 12
            out.append(b"%010d 00000 n " % offset)
        else:
            out.append(line)
    return head + b"\nxref\n" + b"\n".join(out) + b"trailer" + tail


@generator("xref-missing.pdf")
def xref_missing():
    """No cross-reference table at all: the `xref` keyword through the entries is gone, the
    trailer and `startxref` remain and point where the table used to be.

    The whole table has to be reconstructed by scanning for `N 0 obj`, which is `XrefRepair`'s
    widest path and the one whose `RepairReport` says *"reconstructed the entire xref"* rather
    than *"reconstructed 3 objects of 4,000"* -- the two extremes A-22 asks for."""
    data = _base_bytes()
    head, body, tail = _split_xref(data)
    return head + b"\n" + b"trailer" + tail


@generator("startxref-into-object.pdf")
def startxref_into_object():
    """`startxref` points into the middle of object 3 instead of at the table.

    Distinct from a missing table: the table IS there and is correct, and a reader that trusts
    `startxref` finds a dictionary where it expected `xref`. Recovering means searching backwards
    for the real one, which is a different repair from rebuilding."""
    data = _base_bytes()
    marker = data.index(b"3 0 obj")
    prefix, _, _ = data.rpartition(b"startxref")
    return prefix + b"startxref\n%d\n%%%%EOF\n" % (marker + 9)


@generator("startxref-past-eof.pdf")
def startxref_past_eof():
    """`startxref` points beyond the end of the file -- what a truncated download leaves behind
    when the trailer survived in a cache. A reader that seeks there and reads must not fault."""
    data = _base_bytes()
    prefix, _, _ = data.rpartition(b"startxref")
    return prefix + b"startxref\n%d\n%%%%EOF\n" % (len(data) + 4096)


@generator("circular-prev.pdf")
def circular_prev():
    """An incremental file whose second revision's `/Prev` points at ITSELF.

    The termination case for the `/Prev` walk (T7). A reader that follows the chain without
    remembering where it has been loops forever, and a hang is worse than a crash: A-21 asks for
    no crash AND no hang, and this is the file that can produce the second without the first."""
    pdf, catalog, info, pages_number, page_numbers = _healthy(pages=2)
    base = pdf.serialize(catalog, info)
    # Object 3 is the first page's content stream -- see `_healthy`'s allocation order.
    replacement = pb.Stream([], pb.show_text(48, 700, 12, "second revision") + b"\n")
    updated = pb.incremental_update(base, [(3, replacement)], root=catalog, info=info)
    # Find this revision's own startxref and point /Prev at it.
    own = int(updated.rsplit(b"startxref", 1)[1].split(b"%%EOF")[0].strip())
    head, _, tail = updated.rpartition(b"/Prev ")
    old_value, _, rest = tail.partition(b" ")
    return head + b"/Prev %d " % own + rest


@generator("free-list-broken.pdf")
def free_list_broken():
    """The free-object head points at object 9, which is in use, and object 9's entry is still
    marked `n`. A reader that walks the free list to allocate is walking into live data."""
    data = _base_bytes()
    head, body, tail = _split_xref(data)
    lines = body.split(b"\n")
    lines[1] = b"0000000009 65535 f "
    return head + b"\nxref\n" + b"\n".join(lines) + b"trailer" + tail


@generator("size-too-small.pdf")
def size_too_small():
    """The trailer's `/Size` is 4 in a file with a dozen objects. Everything above index 4 is,
    by the trailer's own account, not there -- while the table lists it and the objects exist."""
    data = _base_bytes()
    head, _, tail = data.rpartition(b"/Size ")
    value, _, rest = tail.partition(b" ")
    return head + b"/Size 4 " + rest


# ---------------------------------------------------------------------------------------------
# Object and stream damage
# ---------------------------------------------------------------------------------------------

@generator("length-lies-short.pdf")
def length_lies_short():
    """A content stream whose `/Length` is twelve bytes short of the truth.

    A-17's fixture, and its control is `ordinary/stream-length-control.pdf` -- the SAME document
    with the correct value, so the pair differs in exactly one number. This is the most common
    real-world malformation there is, and recovering it means finding `endstream` rather than
    trusting the dictionary.

    The control is IMPORTED rather than rebuilt: two hand-kept copies of "the same document" drift
    the first time either is edited, and then the control stops controlling anything.
    """
    data = _ordinary("stream-length-control.pdf")
    marker = data.index(b"/Length ")
    end = data.index(b" ", marker + 8)
    declared = int(data[marker + 8:end])
    return data[:marker] + b"/Length %d" % (declared - 12) + data[end:]


@generator("length-lies-long.pdf")
def length_lies_long():
    """The same stream with `/Length` 400 bytes too LONG -- so the declared stream runs off the
    end of the object and into the next one.

    The mirror of the file above and a genuinely different repair: too short leaves data behind,
    too long consumes structure, and a reader that clamps to `endstream` handles one but not
    necessarily the other."""
    data = length_lies_short()
    # Recover the real length from the file we just built, then overshoot instead of undershoot.
    marker = data.index(b"/Length ")
    end = data.index(b" ", marker + 8)
    declared = int(data[marker + 8:end])
    return data.replace(b"/Length %d" % declared, b"/Length %d" % (declared + 412), 1)


@generator("truncated-mid-stream.pdf")
def truncated_mid_stream():
    """The file simply stops in the middle of a content stream: no `endstream`, no `endobj`, no
    xref, no trailer, no `%%EOF`. An interrupted download, which is the single most common damaged
    file a reader is handed."""
    data = _base_bytes()
    cut = data.index(b"stream\n", data.index(b"4 0 obj")) + 300
    return data[:cut]


@generator("no-eof-marker.pdf")
def no_eof_marker():
    """Everything is intact except the trailing `%%EOF`. Readers that scan backwards for it from
    the end of the file find nothing and must fall back rather than give up."""
    data = _base_bytes()
    return data.rsplit(b"%%EOF", 1)[0]


@generator("junk-before-header.pdf")
def junk_before_header():
    """1,024 bytes of HTTP-response-shaped junk before `%PDF-`.

    Every xref offset in the file is now wrong by exactly 1,024 -- which is the recoverable form
    of this damage, because the whole table is off by a constant a reader can measure. ISO 32000
    allows the header to be up to 1,024 bytes in; this sits right at that boundary on purpose."""
    prefix = (b"HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\n"
              b"X-Corpus-Note: this junk is the damage; the PDF below is intact\r\n\r\n")
    prefix += b"." * (1024 - len(prefix))
    return prefix + _base_bytes()


@generator("missing-endobj.pdf")
def missing_endobj():
    """Object 5's `endobj` keyword is gone, so object 6 begins with no terminator before it.

    A lexer that requires `endobj` to close an object silently swallows the next one; a tolerant
    one notices `6 0 obj` and recovers. `pdf/cos/Lexer` (T5) is specified as tolerant by design,
    and this is the file that says what that means."""
    data = _base_bytes()
    marker = data.index(b"5 0 obj")
    end = data.index(b"\nendobj\n", marker)
    return data[:end] + b"\n" + data[end + len(b"\nendobj\n"):]


@generator("duplicate-object-number.pdf")
def duplicate_object_number():
    """Two objects both numbered 5, at different offsets, with the xref naming the first.

    Real files acquire this through a bad merge. The rule -- the one the xref points at wins, not
    the last one in the file -- is exactly what a reader that rebuilds by scanning gets wrong."""
    data = _base_bytes()
    marker = data.index(b"5 0 obj")
    end = data.index(b"endobj\n", marker) + len(b"endobj\n")
    impostor = b"5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>\nendobj\n"
    return data[:end] + impostor + data[end:]


@generator("bad-flate-stream.pdf")
def bad_flate_stream():
    """A `/FlateDecode` stream whose compressed bytes have been corrupted in the middle.

    The Adler-32 at the end will not match and the Huffman decode fails partway, so the interesting
    question is whether the reader keeps the bytes it did decode or throws them away. Nothing else
    in the file is wrong, which is what makes the answer visible."""
    pdf, catalog, info, _, _ = _healthy(pages=2, uncompressed=False)
    data = pdf.serialize(catalog, info)
    start = data.index(b"stream\n", data.index(b"/FlateDecode")) + len(b"stream\n")
    middle = start + 40
    return data[:middle] + bytes((data[middle] ^ 0xFF, data[middle + 1] ^ 0xA5)) \
        + data[middle + 2:]


@generator("unbalanced-dictionary.pdf")
def unbalanced_dictionary():
    """A page dictionary whose `>>` is missing: the object runs to `endobj` with the dictionary
    still open. A lexer with no depth limit reads the rest of the file as one dictionary."""
    data = _base_bytes()
    marker = data.index(b"/Type /Page ")
    # The page object's OWN closer, which is the last `>>` before its `endobj` -- not the first
    # `>>` after `/Type /Page`, which closes the nested /Font dictionary and would be a different
    # (deeper) fault than the one this file is named for.
    end_of_object = data.index(b"\nendobj", marker)
    closer = data.rindex(b">>", marker, end_of_object)
    return data[:closer] + b"  " + data[closer + 2:]


@generator("bad-name-hex-escape.pdf")
def bad_name_hex_escape():
    """A name containing `#GG` -- a `#` escape whose two digits are not hexadecimal.

    ISO 32000 says a name's `#` introduces two hex digits and says nothing about what to do when
    they are not. Every reader invents an answer; ours should be a named one rather than an
    accident."""
    data = _base_bytes()
    return data.replace(b"/Helvetica", b"/Helve#GGtica", 1)


@generator("deep-nesting.pdf")
def deep_nesting():
    """An object holding an array nested 4,000 levels deep.

    Legal PDF -- there is no depth limit in ISO 32000 -- and fatal to a recursive-descent parser
    with no guard, which blows the stack rather than reporting anything. That failure mode is a
    CRASH, which is what A-21 forbids, so the guard is not optional and this is the file that
    proves it exists. 4,000 is chosen to be comfortably past any reasonable limit while keeping
    the file small."""
    pdf, catalog, info, pages_number, page_numbers = _healthy(pages=1)
    nested = b"[" * 4000 + b"0" + b"]" * 4000
    pdf.add(pb.dictionary([(b"/Type", pb.name("CorpusDepthProbe")), (b"/Deep", nested)]))
    return pdf.serialize(catalog, info)


@generator("huge-length-claim.pdf")
def huge_length_claim():
    """A stream declaring `/Length 4294967295` in a file of a few thousand bytes.

    The unbounded-allocation case. A reader that reserves what the dictionary asks for takes four
    gigabytes for a two-kilobyte file, and A-21's *"no unbounded allocation"* is the clause this
    file exists to exercise."""
    data = _base_bytes()
    marker = data.index(b"/Length ")
    end = data.index(b" ", marker + 8)
    declared = data[marker:end]
    return data.replace(declared, b"/Length 4294967295", 1)


@generator("pages-count-wrong.pdf")
def pages_count_wrong():
    """`/Pages` declares `/Count 97` over three real kids.

    The page count is what a viewer puts in the status bar before it has walked the tree, so this
    is the file that decides whether the tree or the number is authoritative -- and the answer has
    to be the tree."""
    data = _base_bytes()
    return data.replace(b"/Count 3", b"/Count 97", 1)


@generator("pages-loop.pdf")
def pages_loop():
    """A page tree with a cycle: an intermediate /Pages node lists its own ancestor as a kid.

    Walking it without a visited set does not terminate. Like `circular-prev.pdf`, the failure
    this catches is a hang rather than a crash."""
    pdf = pb.Pdf()
    regular = pb.base14(pdf, "Helvetica")
    resources = pb.dictionary([(b"/Font", pb.dictionary([(b"/F1", pb.ref(regular))]))])
    root = pdf.reserve()
    branch = pdf.reserve()
    contents = pb.content_stream(
        pdf, pb.show_text(48, 700, 12, "pages-loop.pdf - the tree below contains a cycle") + b"\n",
        compressed=False)
    leaf = pdf.add(pb.page(branch, pb.A4, contents, resources))
    # The cycle: `branch` lists `root`, its own parent, among its kids.
    pdf.set(branch, pb.dictionary([
        (b"/Type", b"/Pages"), (b"/Parent", pb.ref(root)),
        (b"/Kids", pb.array([pb.ref(leaf), pb.ref(root)])), (b"/Count", b"2")]))
    pdf.set(root, pb.dictionary([
        (b"/Type", b"/Pages"), (b"/Kids", pb.array([pb.ref(branch)])), (b"/Count", b"2")]))
    catalog = pdf.add(pb.dictionary([(b"/Type", b"/Catalog"), (b"/Pages", pb.ref(root))]))
    return pdf.serialize(catalog, pb.info_object(pdf, "RawPage corpus: cyclic page tree"))


@generator("no-root.pdf")
def no_root():
    """The trailer has no `/Root`. Every object is intact and there is no way in without finding
    the `/Type /Catalog` object by scanning -- which is a recovery, and should be reported as one
    rather than performed silently."""
    data = _base_bytes()
    head, _, tail = data.rpartition(b"/Root ")
    # "N 0 R " -- three tokens, all of which have to go. Removing only the number leaves a stray
    # "0 R" in the trailer, which would be a SECOND fault and break this directory's one rule.
    rest = tail.split(b" ", 3)[3]
    return head + rest


@generator("root-points-at-nothing.pdf")
def root_points_at_nothing():
    """`/Root 97 0 R` in a file whose highest object number is far below 97.

    Different from a missing /Root and worth separating: here the trailer is confidently wrong,
    and a reader that resolves a dangling reference to a null object gets a document with no
    catalog and no error."""
    data = _base_bytes()
    head, _, tail = data.rpartition(b"/Root ")
    rest = tail.split(b" ", 3)[3]
    return head + b"/Root 97 0 R " + rest


@generator("objstm-corrupt.pdf")
def objstm_corrupt():
    """A PDF 1.5 file whose object stream's `/First` offset is wrong by 8 bytes, so every packed
    object is read from the wrong place.

    Nothing in the classic-xref half of the reader touches this path, which is exactly why it
    needs its own damaged fixture rather than being assumed covered."""
    pdf, catalog, info, _, _ = _healthy(pages=2, version="1.5", uncompressed=False)
    data = pdf.serialize(catalog, info, objstm=True)
    marker = data.index(b"/First ")
    end = data.index(b" ", marker + 7)
    declared = int(data[marker + 7:end])
    return data[:marker] + b"/First %d" % (declared + 8) + data[end:]


def generate():
    return {"malformed/" + name: builder() for name, builder in FILES.items()}


if __name__ == "__main__":
    root = pathlib.Path(__file__).resolve().parent
    (root / "malformed").mkdir(exist_ok=True)
    for relative, data in generate().items():
        target = root / relative
        target.write_bytes(data)
        print("wrote %-34s %8d bytes" % (relative, len(data)))
