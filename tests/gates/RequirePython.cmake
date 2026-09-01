# Stands in for the three gates.corpus.* cases when no Python 3.9+ interpreter was found at
# configure time, so that they FAIL rather than quietly not existing.
#
# WHY A RED TEST IS THE RIGHT ANSWER HERE. Stage 1's I-30 was a CI job that had never run,
# because two systems had to agree on a variable name and nothing checked that they did; the
# whole stage read as covered while three quarters of its test surface was unexercised. A test
# that vanishes when a tool is missing has precisely that shape.
#
# This costs nothing to a build: Python is NOT a build dependency of RawPage -- `cmake --build`
# never invokes it, and CLAUDE.md's "a build box needs Qt and vcpkg, nothing else" stays true.
# It is a VERIFICATION dependency, and the honest report for a verification that could not run
# is a failure, not a pass.

message(FATAL_ERROR
    "no Python 3.9+ interpreter was found at configure time, so the corpus checks could not "
    "run.\n"
    "corpus/ is entirely generated (decision D2-11, claude-plan/Stage2-Rendering-Plan.md), and "
    "Stage 2 T4 requires that every file regenerates BYTE-IDENTICALLY -- asserted, not assumed. "
    "Without an interpreter that property is unchecked, and an unchecked property that reports "
    "green is worse than one that reports red.\n"
    "Install Python 3.9 or newer and re-run `cmake --preset <preset>`, or run the checks by "
    "hand:\n"
    "    python corpus/make-corpus.py --verify\n"
    "    python corpus/make-corpus.py --check-provenance\n"
    "    python corpus/make-corpus.py --self-test")
