# Risk A4 through the PRODUCT's save path, the automated half: the product saves, then OpenSSL judges.
# Guide case A-57 (claude-plan/Stage3-Mutation-Testing-Guide.md §5.9), decision D3-1's second half.
#
# tests/unit/RunA4Probe.cmake's shape, over the other half of D3-1: there the writer alone wrote; here
# tst_saveengine's A-57 case opens each corpus/signed/ file with the SHIPPED backend, rotates it through
# CommandJournal and saves it through SaveCoordinator and the real WinDurableFiles -- and asserts from
# the bytes read back off disk that the opened file is a byte-identical prefix. Then
# tests/gates/CheckSignatures.cmake runs over the four saved files: the same resolver, extractor and
# flipped-byte controls FIRST as guide case A-15.
#
# A rotation is what Acrobat's policy rejects after these signatures from any writer (guide issue I-06),
# so OpenSSL's verdict here is about the signed BYTES; Acrobat's grading of the product is manual case
# M-03's product walk, at T13.
#
# Required: -DTEST_BINARY=<rawpage_render_tests> -DPRODUCT_DIR=<dir> -DCHECKS_DIR=<tests/gates>
# Optional: -DCASE=<the saving case's name> -- Stage 6 T3's guide A-33 runs the same judgement over the
# case that adds a COMMENT, the edit Acrobat's policy permits after an approval signature.

foreach(required TEST_BINARY PRODUCT_DIR CHECKS_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "RunA4Product.cmake: ${required} is required")
    endif()
endforeach()

set(file_list "")
foreach(name approval.pdf approval-xrefstream.pdf certified-nochanges.pdf approval-then-update.pdf)
    file(REMOVE "${PRODUCT_DIR}/${name}")
    list(APPEND file_list "${PRODUCT_DIR}/${name}")
endforeach()

if(NOT DEFINED CASE)
    set(CASE "A4 product saves a rotation into every signed corpus file")
endif()
execute_process(
    COMMAND "${TEST_BINARY}" "${CASE}"
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the A4 product's saving case failed (exit ${rc}); nothing was judged")
endif()
foreach(path IN LISTS file_list)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "the A4 product case did not write ${path}; nothing was judged")
    endif()
endforeach()

message("A4 product outputs, for guide case M-03's product walk: ${PRODUCT_DIR}")
string(REPLACE ";" "@@" FILES "${file_list}")
include("${CHECKS_DIR}/CheckSignatures.cmake")
