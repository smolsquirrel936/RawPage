# rawpage_assert_fuzz_target_count() -- the fuzz build graph has exactly the targets it says it has.
#
# Stage 2 guide case A-27 (`Stage2-Rendering-Testing-Guide.md` §5.4) and Stage 3 guide case A-83(a)
# (`Stage3-Mutation-Testing-Guide.md` §5.12): "ten minutes with no crashes" is passed trivially by a
# target that builds nothing, so ZERO -- or one fewer than expected -- is a configure failure, not a pass.
#
# WHY A FUNCTION IN cmake/ RATHER THAN SIX LINES IN tests/fuzz/CMakeLists.txt, where it lived from
# Stage 2 T5 to Stage 3 T13: that is the only CMakeLists `portable-fuzz` reads and no other preset does,
# so the assertion could only ever be exercised on a Linux runner with clang, and nothing had ever shown
# it FIRES. Moved here, `tests/gates/fixtures/fuzz-count/` includes this file -- not a copy
# (convention 7, Stage 0's I-15) -- and the `gates.fuzz_count.*` cases drive it over a LANGUAGES NONE
# project on every preset, Windows and the bare Linux gates runner alike.
#
# It counts the CALLING DIRECTORY's own build-system targets. A helper executable in a subdirectory is
# not a fuzz target and is not counted, which is why `rawpage_fuzz_seeds` lives in tests/fuzz/seeds/.

function(rawpage_assert_fuzz_target_count expected)
    get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    list(LENGTH _targets _count)
    if(NOT _count EQUAL expected)
        # The last word is what the gate cases match: message(FATAL_ERROR) hard-wraps, so a phrase can
        # be split across a line break (Stage 2 I-09).
        message(FATAL_ERROR
            "\ntests/fuzz declares ${_count} target(s) [${_targets}]; ${expected} expected.\n\n"
            "Zero fuzz targets -- or fewer than expected -- is a FAILURE rather than a pass, because "
            "\"ten minutes with no crashes\" is satisfied by a target that does not exist (Stage 2 "
            "guide A-27, Stage 3 guide A-83). If a target was added or removed on purpose, change "
            "RAWPAGE_EXPECTED_FUZZ_TARGETS in tests/fuzz/CMakeLists.txt in the same commit: miscounted\n")
    endif()
    message(STATUS "  fuzz targets    ${_targets} (${_count}, as expected)")
endfunction()
