#!/usr/bin/env python3
"""Judges the files RawPage's writer wrote with `qpdf --check`. Stage 6 T3, decision D6-6, guide case A-31.

    check-qpdf.py --qpdf <exe> FILE...

WHY QPDF. Every annotation dictionary and appearance stream Stage 6 writes is new first-party output,
read by other people's readers. `RevisionWriter::verify` re-reads it through `pdf/cos/` -- the reader
the writer was built beside, which agrees with it by construction -- and PDFium is the renderer that
will draw it in RawPage. `qpdf` (Apache-2.0) is a third reader that shares nothing with either: its
`--check` parses every object, follows every cross-reference section, and decodes every stream. It is a
VERIFICATION tool -- never linked, never shipped -- as OpenSSL is for signatures (D3-2). Approved as
the plan's Q8 on 2026-10-04.

WHAT IT CHECKS, AND IN WHAT ORDER:

  1. **The version**, printed, so a log says which qpdf judged.
  2. **The control, first.** A copy of the first file with five bytes cut from inside its LAST stream --
     the update's newest object, so its `/Length` now overruns -- must FAIL: a nonzero exit, or no
     "No syntax or stream encoding errors found" line. A checker that passes a truncated stream is not
     checking streams, and the run STOPS there (Stage 2 I-41: an instrument is shown a known answer
     before its reading means anything).
  3. **Every file**: exit 0 AND the line. qpdf exits 3 for warnings -- a repaired xref, a recovered
     length -- and a warning is a defect in a file we wrote, so it fails too.

At least one file is required: a judgement over no files is a green tick that checked nothing.
"""

import argparse
import pathlib
import subprocess
import sys
import tempfile

CLEAN = "No syntax or stream encoding errors found"


def run_check(qpdf, path):
    completed = subprocess.run([qpdf, "--check", str(path)], capture_output=True, text=True)
    output = (completed.stdout or "") + (completed.stderr or "")
    return completed.returncode, output


def truncated_copy(source, directory):
    data = bytearray(source.read_bytes())
    end = data.rfind(b"endstream")
    if end < 0:
        return None
    start = data.rfind(b"stream", 0, end)
    # Cut inside the data, well clear of the keywords: after "stream\n" and before "\nendstream".
    body_start = start + len(b"stream") + 1
    body_end = end - 1
    if body_end - body_start < 10:
        return None
    cut = body_start + (body_end - body_start) // 2
    del data[cut:cut + 5]
    target = pathlib.Path(directory) / ("control-truncated-" + source.name)
    target.write_bytes(bytes(data))
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--qpdf", required=True)
    parser.add_argument("files", nargs="*")
    args = parser.parse_args()

    files = sorted(pathlib.Path(f) for f in args.files)
    if not files:
        print("check-qpdf: no files to judge -- the suite wrote nothing, so nothing was checked")
        return 1

    version = subprocess.run([args.qpdf, "--version"], capture_output=True, text=True)
    first_line = (version.stdout or version.stderr or "").splitlines()
    print("qpdf: " + (first_line[0] if first_line else "(no version output)"))

    with tempfile.TemporaryDirectory() as scratch:
        control = None
        for candidate in files:
            control = truncated_copy(candidate, scratch)
            if control is not None:
                break
        if control is None:
            print("check-qpdf: no file holds a stream long enough to truncate; the control cannot run")
            return 1
        code, output = run_check(args.qpdf, control)
        if code == 0 and CLEAN in output:
            print("control   " + control.name + ": ACCEPTED -- qpdf passed a truncated stream; nothing it says counts")
            print(output)
            return 1
        print("control   " + control.name + ": rejected (exit %d)" % code)

    failed = 0
    for path in files:
        code, output = run_check(args.qpdf, path)
        if code == 0 and CLEAN in output:
            print("check     " + path.name + ": " + CLEAN)
        else:
            failed += 1
            print("check     " + path.name + ": FAILED (exit %d)" % code)
            print(output.rstrip())
    print("qpdf check: %d file(s), %d failed; the truncated-stream control rejected" % (len(files), failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
