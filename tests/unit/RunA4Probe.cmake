# Risk A4's PROBE, end to end: the writer writes, then OpenSSL judges what it wrote.
# Guide case A-28 (claude-plan/Stage3-Mutation-Testing-Guide.md §5.4), decision D3-1's first half.
#
# Two steps in one CTest case, in order, because the second is meaningless without the first:
#
#   1. Run tst_incrementalwriter's probe case, which writes a page rotation into each of the four
#      corpus/signed/ files with pdf/cos/IncrementalWriter ALONE and puts the results in PROBE_DIR,
#      asserting from the bytes read back off disk that every signed range is untouched.
#   2. Include tests/gates/CheckSignatures.cmake over those four files: the same resolver, the same
#      byte-level extractor and the same flipped-byte controls FIRST as guide case A-15 -- so the
#      instrument that said the originals verify is the instrument that judges the probe.
#
# The output files are deleted before step 1, so a stale probe from an earlier build can never be
# what OpenSSL passes. Acrobat's half is guide case M-03's probe walk, over the same PROBE_DIR,
# and is the user's.
#
# Required: -DTEST_BINARY=<rawpage_tests> -DPROBE_DIR=<dir> -DCHECKS_DIR=<tests/gates>

foreach(required TEST_BINARY PROBE_DIR CHECKS_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "RunA4Probe.cmake: ${required} is required")
    endif()
endforeach()

set(probe_files approval.pdf approval-xrefstream.pdf certified-nochanges.pdf approval-then-update.pdf)
set(file_list "")
foreach(name IN LISTS probe_files)
    file(REMOVE "${PROBE_DIR}/${name}")
    list(APPEND file_list "${PROBE_DIR}/${name}")
endforeach()

execute_process(
    COMMAND "${TEST_BINARY}" "A4 probe writes a page rotation into every signed corpus file"
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the A4 probe's writing case failed (exit ${rc}); nothing was judged")
endif()
foreach(path IN LISTS file_list)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "the A4 probe did not write ${path}; nothing was judged")
    endif()
endforeach()

message("A4 probe outputs, for guide case M-03's probe walk: ${PROBE_DIR}")
string(REPLACE ";" "@@" FILES "${file_list}")
include("${CHECKS_DIR}/CheckSignatures.cmake")
