# cmake/RawPageLanguageStandard.cmake -- every compiled target in this project is C++20, asserted at
# configure time. Stage 4 T12, issue I-08 (`Stage4-ViewingSurface-Testing-Guide.md` §8).
#
# -----------------------------------------------------------------------------------------------
# WHY THIS FILE EXISTS: THE SAME DEFECT TWICE IN ONE DAY, IN TWO TARGETS, INVISIBLE BOTH TIMES
# -----------------------------------------------------------------------------------------------
# `rawpage_common_flags()` is the one place `CXX_STANDARD 20` is set. A target that does not call it
# -- directly, or through `rawpage_strict()` / `rawpage_ui_grace()` / `rawpage_app_flags()` -- is
# compiled at the COMPILER'S DEFAULT standard: `gnu++17` for GCC 13, and clang's own default on the
# portable presets. Nothing says so. The target builds, its tests pass, and it goes on being a
# different language from the product it is testing.
#
# Two targets were in that state and neither was noticed for weeks:
#
#   * `rawpage_fuzz_seeds` (Stage 3 T13) simply never called it.
#   * The three `tests/fuzz` drivers skipped `rawpage_strict()` **on purpose** -- `-Werror` on a
#     sanitizer's own warnings is not wanted -- and skipped the standard as collateral, because the
#     two arrive through one function.
#
# Both stopped building on the same day, when `core/backend.h` gained `core/Geometry.h` and
# `operator==() = default` reached translation units that had never needed C++20 before. **A compile
# error is the honest form of this defect**; the form that had been running is the dishonest one.
#
# -----------------------------------------------------------------------------------------------
# WHY AN ASSERTION RATHER THAN A FIXED COMMENT
# -----------------------------------------------------------------------------------------------
# I-08's own entry says the class outlives the instance, and a fix applied to the two known targets
# is a fix that waits for the third. A comment saying *remember to call it* is what the file it would
# have been written in already said. So this walks the real build graph -- every directory, every
# compiled target -- and **fails configure** naming the target and the call to add.
#
# It reads the graph rather than the CMakeLists text, which is the same reason `layer-lint` reads the
# includes the compiler reads: a check on the source of the build files would have to model
# `add_subdirectory`, functions and conditionals to answer a question CMake can answer exactly.
#
# WHAT IT DOES NOT COVER, named rather than discovered (convention 19): only `CXX_STANDARD`. It says
# nothing about whether a target got the WARNING flags, the charset flags or `-g`, all of which are
# also `rawpage_common_flags`' -- a target could still be missing every one of those and pass here.
# The standard is singled out because it silently changes the meaning of valid code, which the others
# do not: a missing `-Wshadow` loses a warning, a missing `-std=c++20` loses a language.

# Collects every buildsystem target under `dir` and its subdirectories into `out_var`.
function(_rawpage_collect_targets dir out_var)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(children DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        _rawpage_collect_targets("${child}" child_targets)
        list(APPEND targets ${child_targets})
    endforeach()
    set(${out_var} "${targets}" PARENT_SCOPE)
endfunction()

# Call from the ROOT CMakeLists, AFTER every `add_subdirectory` -- a call made earlier sees a partial
# graph and passes by accident, which is the trap `rawpage_write_qt_module_list()` records beside it.
function(rawpage_assert_language_standard expected)
    _rawpage_collect_targets("${CMAKE_SOURCE_DIR}" all_targets)

    set(offenders "")
    foreach(target IN LISTS all_targets)
        # Only targets that COMPILE something can carry a standard. `checks` and the other custom
        # targets are UTILITY, an interface library compiles nothing of its own, and an imported
        # target (Qt, Catch2) is somebody else's build.
        get_target_property(type ${target} TYPE)
        if(NOT type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
            continue()
        endif()
        get_target_property(imported ${target} IMPORTED)
        if(imported)
            continue()
        endif()

        get_target_property(standard ${target} CXX_STANDARD)
        if(NOT standard STREQUAL "${expected}")
            if(standard STREQUAL "standard-NOTFOUND")
                set(standard "unset -- the compiler's default")
            endif()
            list(APPEND offenders "${target} (CXX_STANDARD ${standard})")
        endif()
    endforeach()

    list(LENGTH all_targets total)
    if(offenders)
        string(REPLACE ";" "\n    " pretty "${offenders}")
        message(FATAL_ERROR
            "RawPage language standard: these targets are not C++${expected} --\n"
            "    ${pretty}\n"
            "\n"
            "Every compiled target in this project is C++${expected}, and `rawpage_common_flags()` is the\n"
            "one place that sets it. Call it -- or `rawpage_strict()`, which calls it and adds -Werror --\n"
            "in the target's own CMakeLists.\n"
            "\n"
            "A target compiled at the compiler's default standard BUILDS, and goes on building until\n"
            "some header it includes uses a C++${expected} feature. That is issue I-08 in\n"
            "claude-plan/Stage4-ViewingSurface-Testing-Guide.md section 8, which happened twice before\n"
            "this assertion existed: rawpage_fuzz_seeds, which never called it, and the tests/fuzz\n"
            "drivers, which skipped rawpage_strict() deliberately to avoid -Werror and lost the\n"
            "standard with it.\n"
            "\n"
            "If a target genuinely must differ, that is a decision for a plan and not for a diff:\n"
            "record it, then exempt it here by name so the exemption is visible.")
    endif()

    message(STATUS "  language        C++${expected} on every compiled target (${total} targets walked)")
endfunction()
