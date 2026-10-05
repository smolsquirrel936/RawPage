#!/usr/bin/env python3
"""Refuses a control byte in any tracked TEXT file -- S4 guide I-22.

WHAT IT CATCHES, AND WHERE IT CAME FROM. Four Windows paths in two documents had their backslashes
read as escapes on the way into the file -- a document edited through a script whose string was not
raw -- so `\\b`, `\\r`, `\\a`, `\\v` and `\\x64` became BACKSPACE, CARRIAGE RETURN, BELL, VERTICAL TAB and
`d`. `.\\build\\x64-release\\bin\\rawpage_bench_surface.exe` in CLAUDE.md's Commands became
`.<BS>uildd-release<BS>in<CR>awpage_bench_surface.exe`, and every session loaded it that way for four
days. A terminal draws the damage invisibly -- a backspace and a carriage return overwrite, a bell
makes no mark -- so no reader saw it; it was found by a byte-level read, and this makes that read
permanent.

THE RULE. In a text file, every byte below 0x20 is refused except TAB and LINE FEED, and CARRIAGE
RETURN is allowed only immediately before a line feed: about forty tracked files are CRLF, some of
them mixed, and a line ending is not the defect -- a LONE carriage return is, because anything that
reads in universal-newline mode turns it into a line break (the edit script that found I-22 split two
of CLAUDE.md's lines at exactly those bytes). DEL (0x7f) is refused as well.

WHICH FILES. What `git ls-files` lists -- the tracked tree, so a build directory, a scratch file or a
manual walk's output is never read -- minus the binary types this repository tracks, named by
extension. A NEW binary type fails this check loudly until its extension is added below, which is
the right direction: a guessed classification (git's own NUL heuristic) would pass a text file that
had been damaged with a NUL.

WHAT IT CANNOT SEE: a mangled escape that produced a PRINTABLE character. `\\x64` became `d`, which is
a legal byte; I-22 was caught only because the same path also held a `\\b`. And it reads only the
files git tracks, so a file not yet added is not checked until it is.
"""

import os
import subprocess
import sys

# The binary types this repository tracks, re-derived from `git ls-files` on 2026-09-27 -- the
# golden PNGs, the corpus PDFs, and the pinned toolchain and engine artifacts -- and on 2026-10-05 at
# Stage 6 T2, when this check went red over the two types Stage 6 T1 committed: `.fdf` (COS syntax, its
# streams raw bytes -- an attachment's file, a sound's PCM) and `.jpg` (a custom stamp image). `.xfdf`
# is XML and stays checked (Stage 6 guide I-04).
BINARY_EXTENSIONS = {".png", ".pdf", ".dll", ".a", ".exe", ".fdf", ".jpg"}

NAMES = {0x00: "NUL", 0x07: "BELL", 0x08: "BACKSPACE", 0x0B: "VERTICAL TAB", 0x0C: "FORM FEED",
         0x0D: "LONE CARRIAGE RETURN", 0x1B: "ESCAPE", 0x7F: "DELETE"}


def findings(data):
    """Every offending byte in `data`, as (line, column, byte). Lines and columns count from 1."""
    out = []
    line, column = 1, 0
    for i, byte in enumerate(data):
        column += 1
        if byte == 0x0A:
            line, column = line + 1, 0
            continue
        if byte == 0x09:
            continue
        if byte == 0x0D and i + 1 < len(data) and data[i + 1] == 0x0A:
            continue
        if byte < 0x20 or byte == 0x7F:
            out.append((line, column, byte))
    return out


def describe(byte):
    return "0x%02x %s" % (byte, NAMES.get(byte, "CONTROL"))


def tracked_text_files(root):
    try:
        listed = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True,
                                check=True).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        # A FAILURE, never a skip: a check that finds no files reports a clean tree.
        sys.exit("text-bytes: cannot list the tracked files under %s: %s" % (root, error))
    paths = [p.decode("utf-8") for p in listed.split(b"\0") if p]
    return [p for p in paths if os.path.splitext(p)[1].lower() not in BINARY_EXTENSIONS]


def check_tree(root):
    files = tracked_text_files(root)

    # THE ENUMERATION IS PART OF THE CLAIM. A listing that came back empty, or from the wrong
    # directory, would scan nothing and print "clean"; CLAUDE.md is where I-22 was, and it must be
    # among what was read.
    if "CLAUDE.md" not in files:
        sys.exit("text-bytes: CLAUDE.md is not among the %d files listed under %s -- "
                 "the listing is wrong, so nothing was checked" % (len(files), root))

    bad = 0
    for path in files:
        with open(os.path.join(root, path), "rb") as handle:
            for line, column, byte in findings(handle.read()):
                print("%s:%d:%d: %s" % (path, line, column, describe(byte)))
                bad += 1
    if bad:
        print("text-bytes: %d control byte(s) in tracked text files -- most likely a path whose "
              "backslashes were read as escapes; write it through a raw string" % bad)
        return 1
    print("text bytes clean -- %d tracked text files, no control byte but TAB, LF and CRLF"
          % len(files))
    return 0


def self_test():
    """Each detection driven by an input that must trigger it, and the allowed bytes by inputs that
    must not -- in memory, so the control needs no damaged file in the tree (convention 7)."""
    must_fire = {
        "backspace (\\b)": b"bin\x08in\n",
        "lone carriage return (\\r)": b"bin\rawpage.exe\n",
        "bell (\\a)": b"build\\<preset>\x074-probe\n",
        "vertical tab (\\v)": b"%USERPROFILE%\x0bcpkg\n",
        "form feed (\\f)": b"a\x0cb\n",
        "NUL": b"a\x00b\n",
        "escape": b"a\x1bb\n",
        "delete": b"a\x7fb\n",
        "a CR at the very end": b"last line\r",
    }
    must_pass = {
        "tab": b"a\tb\n",
        "LF": b"one\ntwo\n",
        "CRLF": b"one\r\ntwo\r\n",
        "mixed LF and CRLF": b"one\r\ntwo\nthree\r\n",
        "UTF-8 above 0x7f": "§ — …\n".encode("utf-8"),
    }
    failed = 0
    for name, data in must_fire.items():
        if not findings(data):
            print("DETECTION DID NOT FIRE: %s" % name)
            failed += 1
    for name, data in must_pass.items():
        if findings(data):
            print("CLEAN INPUT REFUSED: %s -> %s" % (name, findings(data)))
            failed += 1

    # And the POSITION, since a report nobody can find is half a report: I-22's own line.
    where = findings(b"line one\n.\x08uildd-release\x08in\rawpage.exe\n")
    if where != [(2, 2, 0x08), (2, 16, 0x08), (2, 19, 0x0D)]:
        print("WRONG POSITIONS: %s" % where)
        failed += 1

    if failed:
        return 1
    print("%d of %d detections fired, %d of %d clean inputs passed, positions exact"
          % (len(must_fire), len(must_fire), len(must_pass), len(must_pass)))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "--self-test":
        return self_test()
    if len(argv) == 3 and argv[1] == "--root":
        return check_tree(argv[2])
    if len(argv) >= 3 and argv[1] == "--files":
        bad = 0
        for path in argv[2:]:
            with open(path, "rb") as handle:
                for line, column, byte in findings(handle.read()):
                    print("%s:%d:%d: %s" % (path, line, column, describe(byte)))
                    bad += 1
        return 1 if bad else 0
    print("usage: check-text-bytes.py --root <repository> | --self-test | --files <path>...",
          file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
