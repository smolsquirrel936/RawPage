#!/usr/bin/env python3
"""The corpus driver: regenerates every file, and PROVES it regenerates identically.

    python corpus/make-corpus.py                  # write every file
    python corpus/make-corpus.py --verify         # regenerate in memory, compare against disk
    python corpus/make-corpus.py --check-provenance
    python corpus/make-corpus.py --check-faults    # the Stage 5 and 6 fixtures carry what they claim
    python corpus/make-corpus.py --list

WHY `--verify` IS THE POINT OF THIS FILE. D2-11 (`Stage2-Rendering-Plan.md` section 3) makes every
corpus file generated, and Stage 2 T4's exit criterion adds the clause that matters: *every file
regenerates byte-identically from a clean checkout -- that reproducibility is the property the
whole decision rests on, and it is ASSERTED rather than assumed*. `--verify` is that assertion, and
`gates.corpus.reproducible` is where CTest runs it.

What it catches, in the order it is likely to matter:

  * a generator that is not deterministic -- a clock, a `set`, a dict ordered by hash, `random`,
    or a `zlib` whose output differs between this box and the Ubuntu runner (which is why
    `corpus/deflate.py` exists);
  * a corpus file EDITED BY HAND. A PDF xref stores absolute byte offsets, so a one-character edit
    invalidates every offset below it -- `corpus/make-minimal.py` has said so since Stage 0, and
    until now nothing checked;
  * `.gitattributes` failing to keep `corpus/**` binary. CRLF injection on checkout shifts every
    offset, and the file becomes a DIFFERENT malformed file than the test was written against;
  * a generator quietly dropped from the build, or an output file nobody generates any more.

The last of those is why `--verify` also fails on an ORPHAN -- a `.pdf` on disk that no generator
claims. A corpus that can accumulate files from nowhere is a corpus where D2-11's "every file is
ours" has stopped being checkable, which is Stage 1's I-30 in yet another costume: the check that
reports success because it never looked.
"""

import argparse
import hashlib
import importlib.util
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))


def _load(filename):
    """Import a `make-*.py` script by path. Their hyphens make them un-importable by name, and
    renaming them would break the Stage 0 and T3 documents that quote the two oldest by name."""
    spec = importlib.util.spec_from_file_location(
        "_corpus_" + filename.replace("-", "_").replace(".py", ""), ROOT / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def build_all():
    """{relative path: bytes} for every file in the corpus, in a stable order.

    The two Stage 0 / T3 generators are called through their `build()` rather than being ported
    onto `pdfbuild`: `minimal.pdf`'s hash is quoted in `Stage0-Toolchain-Testing-Guide.md` and
    `a2-text-page.pdf` is the A2 probe's input in `Tech-Stack-Plan.md` section 3.3. Rewriting
    either would change bytes those documents name, for no gain -- and "it regenerates identically"
    would stop being checkable for exactly the two files that have been claiming it longest.
    """
    files = {}
    files["minimal.pdf"] = _load("make-minimal.py").build()
    files["a2-text-page.pdf"] = _load("make-a2-text-page.py").build()
    for script in ("make-ordinary.py", "make-malformed.py", "make-encrypted.py", "make-signed.py",
                   "make-text.py", "make-compare.py", "make-annots.py"):
        files.update(_load(script).generate())
    return files


# What --verify treats as corpus DATA, orphans included. Stage 5 T1 added the two JSON kinds: a
# label (`labels/`) is the ground truth for a text fixture and an `expected.json` (`compare/`) the
# change list for a comparison pair, and each is generated beside its PDF and checked byte for byte
# like it -- a hand-edited label is a test whose answer was changed to match.
#
# Stage 6 T1 added two more kinds of data that are not PDFs: the FDF and XFDF exchange files
# (`exchange/`) and the custom-stamp images (`stamps/`). Each is generated, checked byte for byte
# and given a PROVENANCE.md row like a PDF -- an orphan .xfdf is as much a file from nowhere as an
# orphan .pdf.
DATA_GLOBS = ("*.pdf", "labels/*.json", "compare/*.json", "exchange/*.fdf", "exchange/*.xfdf",
              "stamps/*.png", "stamps/*.jpg")

# The file kinds that carry a PROVENANCE.md row of their own. Labels and comparison records do not:
# each is the sidecar of a PDF whose row names it.
ROWED = (".pdf", ".fdf", ".xfdf", ".png", ".jpg")


def _on_disk():
    found = set()
    for pattern in DATA_GLOBS:
        glob = ROOT.rglob(pattern) if pattern == "*.pdf" else ROOT.glob(pattern)
        found.update(str(path.relative_to(ROOT)).replace("\\", "/") for path in glob)
    return sorted(found)


def write(files):
    for relative, data in files.items():
        target = ROOT / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return len(files)


def verify(files, disk=None, rerun=True):
    """Compare freshly generated bytes against what is on disk. Returns a list of complaints.

    `disk` overrides what is read back -- a {path: bytes} mapping standing in for the tree. Only
    `--self-test` passes it, and only so the failure paths below can be driven without touching
    the real corpus.
    """
    if disk is None:
        disk = {relative: (ROOT / relative).read_bytes() for relative in _on_disk()}

    problems = []
    for relative, data in files.items():
        if relative not in disk:
            problems.append("MISSING   %s -- generated but not on disk" % relative)
            continue
        actual = disk[relative]
        if actual != data:
            problems.append(
                "DIFFERS   %s -- on disk %d bytes sha256 %s, regenerated %d bytes sha256 %s"
                % (relative, len(actual), hashlib.sha256(actual).hexdigest()[:16],
                   len(data), hashlib.sha256(data).hexdigest()[:16]))
    for relative in sorted(disk):
        if relative not in files:
            problems.append("ORPHAN    %s -- on disk, and no generator produces it" % relative)

    # Determinism WITHIN one process, which is a different failure from a mismatch against disk:
    # a generator seeded by something per-run gives two different answers in a single run, and
    # would otherwise only be caught on a machine that had already written the first answer.
    if rerun:
        again = build_all()
        for relative, data in files.items():
            if again.get(relative) != data:
                problems.append("UNSTABLE  %s -- two runs in one process disagree" % relative)
    return problems


def provenance_rows():
    """The corpus FILE named in the first cell of a `corpus/PROVENANCE.md` table row.

    Only cells ending in one of `ROWED`'s extensions count. The document also carries a table
    describing the generators and encoders themselves, whose first cells are `corpus/deflate.py` and
    friends -- those are prose about the tooling, not rows about data, and counting them would make
    this check demand that `pdfbuild.py` generate itself.
    """
    text = (ROOT / "PROVENANCE.md").read_text(encoding="utf-8")
    rows = []
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip("|").split("|")]
        if not cells or not cells[0].startswith("`") or not cells[0].endswith("`"):
            continue
        name = cells[0].strip("`")
        if name.endswith(ROWED):
            rows.append(name)
    return rows


def check_provenance(files, rows=None):
    problems = []
    rows = provenance_rows() if rows is None else list(rows)
    duplicates = {name for name in rows if rows.count(name) > 1}
    for name in sorted(duplicates):
        problems.append("DUPLICATE %s -- two PROVENANCE.md rows" % name)
    listed = set(rows)
    rowed = [relative for relative in files if relative.endswith(ROWED)]
    for relative in rowed:
        if relative not in listed:
            problems.append("UNLISTED  %s -- generated, but no PROVENANCE.md row" % relative)
    for name in sorted(listed - set(rowed)):
        problems.append("STALE     %s -- a PROVENANCE.md row for a file nothing generates" % name)
    return problems + check_sidecars(files)


def check_sidecars(files, labelled=None):
    """Stage 5 T1: every label names a corpus file that exists and every text fixture has a label;
    every comparison record names a base and a revised file that exist. A label for a file that is
    gone is ground truth for nothing, and a text fixture with no label is one nobody can grade.
    Stage 6 T1 adds the annotation fixtures, whose labels are `labels/annots-<name>.json`, and the
    migration label, which names a revised document as well."""
    problems = []
    if labelled is None:
        labelled = ["ordinary/" + name for name in _load("make-text.py").FILES]
        labelled += ["ordinary/" + name for name in _load("make-annots.py").FILES]
    for relative, data in files.items():
        if relative.startswith("labels/"):
            body = json.loads(data.decode("utf-8"))
            for key in ("file", "revised"):
                named = body.get(key)
                if (key == "file" or named is not None) and named not in files:
                    problems.append("LABEL     %s -- its %s %r is not in the corpus"
                                    % (relative, key, named))
        elif relative.startswith("compare/") and relative.endswith(".json"):
            record = json.loads(data.decode("utf-8"))
            for key in ("base", "revised"):
                if record.get(key) not in files:
                    problems.append("EXPECTED  %s -- its %s %r is not in the corpus"
                                    % (relative, key, record.get(key)))
    annots = _load("make-annots.py")
    for fixture in labelled:
        name = fixture.split("/")[-1]
        if name in annots.FILES:
            label = annots.label_name(name)
        else:
            label = "labels/" + name.replace(".pdf", ".json")
        if label not in files:
            problems.append("UNLABELLED %s -- a labelled fixture with no %s" % (fixture, label))
    return problems


def check_faults(files):
    import fixturecheck
    return fixturecheck.check(files)


def self_test(files):
    """The negative controls: drive each failure path and require that it FIRES.

    `--verify` passing on a clean tree is equally satisfied by a checker that returns success
    without looking -- which is the shape of Stage 1's I-30 and of every waived gate this project
    has had to re-examine. So each detection gets an input that must trigger it -- the five of Stage
    2 T4, the five Stage 5 T1 added for labels, comparison records and fixture faults, and the four
    Stage 6 T1 added for the annotation fixtures, their labels and the exchange files -- and a
    detection that stays quiet fails this test.

    Nothing here touches the corpus on disk: the stand-in tree and the stand-in row list are
    built in memory, so an interrupted run cannot leave the real files damaged.
    """
    clean = dict(files)
    first = sorted(clean)[0]
    results = []

    def control(name, problems, marker):
        fired = any(problem.startswith(marker) for problem in problems)
        results.append((name, fired, problems[:1]))
        return fired

    damaged = dict(clean)
    damaged[first] = clean[first][:100] + bytes([clean[first][100] ^ 0xFF]) + clean[first][101:]
    control("edited file is DIFFERS", verify(clean, damaged, rerun=False), "DIFFERS")

    absent = dict(clean)
    del absent[first]
    control("deleted file is MISSING", verify(clean, absent, rerun=False), "MISSING")

    extra = dict(clean)
    extra["ordinary/not-generated-by-anything.pdf"] = b"%PDF-1.7\n"
    control("unclaimed file is ORPHAN", verify(clean, extra, rerun=False), "ORPHAN")

    rows = provenance_rows()
    control("undocumented file is UNLISTED",
            check_provenance(clean, [row for row in rows if row != first]), "UNLISTED")
    control("row for no file is STALE",
            check_provenance(clean, rows + ["ordinary/documented-but-absent.pdf"]), "STALE")

    # Stage 5 T1's sidecars and fixtures -- each detection driven by damage that must trigger it.
    orphan_label = dict(clean)
    orphan_label["labels/orphan.json"] = b'{"file": "ordinary/not-in-the-corpus.pdf"}'
    control("label for no file is LABEL", check_sidecars(orphan_label), "LABEL")
    unlabelled = {k: v for k, v in clean.items() if k != "labels/columns-two.json"}
    control("fixture with no label is UNLABELLED", check_sidecars(unlabelled), "UNLABELLED")
    orphan_record = dict(clean)
    orphan_record["compare/orphan.expected.json"] = (
        b'{"base": "compare/base.pdf", "revised": "compare/gone.pdf"}')
    control("record for no file is EXPECTED", check_sidecars(orphan_record), "EXPECTED")
    no_tounicode = dict(clean)
    no_tounicode["ordinary/text-tounicode.pdf"] = clean["ordinary/text-tounicode.pdf"].replace(
        b"/ToUnicode", b"/ToUnicodX")
    control("fixture without its fault is FAULT", check_faults(no_tounicode), "FAULT")
    control_word = dict(clean)
    control_word["ordinary/search-classes.pdf"] = (clean["ordinary/search-classes.pdf"]
                                                   + b"% wombat" + bytes([10]))
    control("control word present is FAULT", check_faults(control_word), "FAULT")

    # Stage 6 T1 (guide A-14, A-15): the two fixture controls the guide names, an annotation
    # fixture with no label, and a non-PDF data file with no PROVENANCE.md row.
    drawn = dict(clean)
    drawn["ordinary/annots-no-ap.pdf"] = clean["ordinary/annots-no-ap.pdf"].replace(
        b"/Contents (no appearance: Highlight)",
        b"/Contents (no appearance: Highlight) /AP << /N 1 0 R >>", 1)
    control("an /AP on annots-no-ap is FAULT", check_faults(drawn),
            "FAULT     ordinary/annots-no-ap.pdf")
    keyless = dict(clean)
    keyless["ordinary/annots-unknown-keys.pdf"] = clean["ordinary/annots-unknown-keys.pdf"].replace(
        b"/RawPageTestKey", b"/RawPageTestKez")
    control("/RawPageTestKey removed is FAULT", check_faults(keyless),
            "FAULT     ordinary/annots-unknown-keys.pdf")
    no_annot_label = {k: v for k, v in clean.items() if k != "labels/annots-threads.json"}
    control("annotation fixture, no label", check_sidecars(no_annot_label),
            "UNLABELLED ordinary/annots-threads.pdf")
    control("unrowed exchange file is UNLISTED",
            check_provenance(clean, [row for row in rows if row != "exchange/threads.xfdf"]),
            "UNLISTED  exchange/threads.xfdf")

    for name, fired, sample in results:
        print("  %-32s %s%s" % (name, "detected" if fired else "NOT DETECTED",
                                ("  <- " + sample[0]) if fired and sample else ""))
    failed = [name for name, fired, _ in results if not fired]
    if failed:
        return ["CONTROL   %s -- the failure path did not fire" % name for name in failed]
    print("corpus self-test ok: %d of %d controls fired" % (len(results), len(results)))
    return []


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--verify", action="store_true",
                        help="regenerate in memory and compare against the files on disk")
    parser.add_argument("--check-provenance", action="store_true",
                        help="every file has a PROVENANCE.md row and every row has a file")
    parser.add_argument("--check-faults", action="store_true",
                        help="the Stage 5 and 6 fixtures carry the fault or feature each claims")
    parser.add_argument("--self-test", action="store_true",
                        help="the negative controls: prove each --verify failure path fires")
    parser.add_argument("--list", action="store_true",
                        help="print every file with its size and sha256")
    arguments = parser.parse_args()

    files = build_all()

    if arguments.list:
        for relative, data in files.items():
            print("%-40s %9d  %s" % (relative, len(data), hashlib.sha256(data).hexdigest()))
        print("%d files" % len(files))
        return 0

    problems = []
    if arguments.verify:
        problems += verify(files)
    if arguments.check_provenance:
        problems += check_provenance(files)
    if arguments.check_faults:
        problems += check_faults(files)
    if arguments.self_test:
        problems += self_test(files)

    if not (arguments.verify or arguments.check_provenance or arguments.check_faults
            or arguments.self_test):
        count = write(files)
        print("wrote %d corpus files under %s" % (count, ROOT))
        return 0

    if problems:
        for problem in problems:
            print(problem)
        print("corpus check FAILED: %d problem(s)" % len(problems))
        return 1

    checked = []
    if arguments.verify:
        checked.append("%d files regenerate byte-identically" % len(files))
    if arguments.check_provenance:
        checked.append("%d PROVENANCE.md rows, one per file" % len(provenance_rows()))
        checked.append("%d labels and %d comparison records name files that exist" % (
            sum(1 for k in files if k.startswith("labels/")),
            sum(1 for k in files if k.startswith("compare/") and k.endswith(".json"))))
    if arguments.check_faults:
        checked.append("every Stage 5 and Stage 6 fixture carries what its row claims")
    if checked:
        print("corpus check ok: " + "; ".join(checked))
    return 0


if __name__ == "__main__":
    sys.exit(main())
