# CheckFuzzWorkflows.cmake -- every fuzz target the build declares is RUN by both workflows that fuzz.
#
#     cmake -DFUZZ_CMAKELISTS=tests/fuzz/CMakeLists.txt -DCI_YML=.github/workflows/ci.yml
#           -DSOAK_YML=.github/workflows/fuzz-soak.yml -P tests/gates/CheckFuzzWorkflows.cmake
#
# Stage 7 guide I-09 (`Stage7-Assembly-Testing-Guide.md` §8). `cmake/RawPageFuzzTargets.cmake` asserts how
# many targets tests/fuzz BUILDS, and that is what it was written for: "ten minutes with no crashes" is
# passed by a target that does not exist. It says nothing about which targets are RUN, and Stage 7 T5's
# `fuzz_assembly` was built, linked and counted by CI run 41180314162 -- "6, as expected" -- and fuzzed
# by neither ci.yml's 60 s step nor fuzz-soak.yml's matrix, under a green tick. Each workflow names its
# targets by hand, so the list a task must extend lives in three files, and two of them are YAML that no
# build reads. This reads all three, as text, the way the gates read the files the compiler reads
# (convention 8), and refuses a target either workflow does not run.
#
# WHAT "RUN" MEANS HERE, and no more: ci.yml has a non-comment line invoking `bin/<target> ` with an
# argument after it and lists the target in its "linked" loop; fuzz-soak.yml has a matrix entry
# `- target: <target>`. It does not check the seeds, the time, or that the step is not skipped by an `if:`.
# Comment lines are dropped first, so a target named only in a comment is not run.

foreach(var FUZZ_CMAKELISTS CI_YML SOAK_YML)
    if(NOT DEFINED ${var} OR NOT EXISTS "${${var}}")
        message(FATAL_ERROR "CheckFuzzWorkflows: -D${var}=<file> is required and must exist (got '${${var}}')")
    endif()
endforeach()

# The text with every comment line removed, a newline in front so `\n` anchors the first line too.
function(_rawpage_read_without_comments path out)
    file(READ "${path}" text)
    string(REPLACE "\r" "" text "${text}")
    set(text "\n${text}\n")
    string(REGEX REPLACE "\n[ \t]*#[^\n]*" "\n" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

_rawpage_read_without_comments("${FUZZ_CMAKELISTS}" cmake_text)
_rawpage_read_without_comments("${CI_YML}" ci_text)
_rawpage_read_without_comments("${SOAK_YML}" soak_text)

string(REGEX MATCHALL "\nrawpage_add_fuzz_target\\([A-Za-z0-9_]+" calls "${cmake_text}")
set(targets "")
foreach(call IN LISTS calls)
    string(REGEX REPLACE "^\nrawpage_add_fuzz_target\\(" "" name "${call}")
    list(APPEND targets "${name}")
endforeach()
list(LENGTH targets count)

# Zero is a failure, not a pass: a reading that matched nothing would otherwise find nothing unrun.
if(count EQUAL 0)
    message(FATAL_ERROR "CheckFuzzWorkflows: no rawpage_add_fuzz_target() call read from ${FUZZ_CMAKELISTS}: nothing")
endif()

string(REGEX MATCH "\n[ \t]*for target in [^\n]*" linked_loop "${ci_text}")
set(missing "")
foreach(name IN LISTS targets)
    string(REGEX MATCH "\n[^\n]*bin/${name} +[^ \n]" ci_run "${ci_text}")
    if(NOT ci_run)
        list(APPEND missing "${name}: no ci.yml step runs it")
    endif()
    string(REGEX MATCH "[ ]${name}[ ;]" ci_linked "${linked_loop} ")
    if(NOT ci_linked)
        list(APPEND missing "${name}: not in ci.yml's linked loop")
    endif()
    string(REGEX MATCH "\n[ \t]*- target: ${name}[ \t]*\n" soak_entry "${soak_text}")
    if(NOT soak_entry)
        list(APPEND missing "${name}: no fuzz-soak.yml matrix entry")
    endif()
endforeach()

if(missing)
    list(JOIN missing "\n  " missing_text)
    # The last word is what the gate cases match: message(FATAL_ERROR) hard-wraps (Stage 2 I-09).
    message(FATAL_ERROR "\n  ${missing_text}\n"
        "A target built and counted but not fuzzed passes every fuzz step it is absent from (Stage 7 I-09). "
        "Add it to both workflows in the commit that adds it: unrun\n")
endif()

# The FIRST directory a libFuzzer run is given is where it WRITES every input it finds. A checked-in corpus there is
# corpus data no provenance row records, and every later step seeded from it reads what an earlier target wrote:
# ci.yml's lexer and parser steps wrote ~1,440 inputs into corpus/malformed/ on every push until Stage 7 guide I-12,
# so fuzz_assembly's crashing seed was one of theirs and no checked-in file at all.
set(writes "")
foreach(workflow ci soak)
    string(REGEX MATCHALL "bin/[A-Za-z0-9_]+ +corpus/[A-Za-z0-9_/.-]*" runs "${${workflow}_text}")
    foreach(run IN LISTS runs)
        string(REGEX MATCH "^bin/([A-Za-z0-9_]+) +(.*)$" pair "${run}")
        list(APPEND writes "${CMAKE_MATCH_1}: its run writes into ${CMAKE_MATCH_2}")
    endforeach()
endforeach()
if(writes)
    list(JOIN writes "\n  " writes_text)
    message(FATAL_ERROR "\n  ${writes_text}\n"
        "libFuzzer writes what it finds into its FIRST directory; pass a scratch directory first and the checked-in "
        "seeds after it (Stage 7 I-12): corpus\n")
endif()

list(JOIN targets " " targets_text)
message(STATUS "fuzz workflows: ${count} target(s), each run by ci.yml and fuzz-soak.yml [${targets_text}]")
