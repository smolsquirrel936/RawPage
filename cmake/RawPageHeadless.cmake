# rawpage_assert_headless() -- the standing proof that core/ needs no display and no PDF engine.
#
# WHAT IT IS FOR. tests/unit is the headless suite: it links rawpage_core and rawpage_cos and
# NOTHING ELSE, and the whole domain suite must pass with no display and no PDF engine linked. That
# is not a preference -- core/ is required to be testable in isolation (Tech-Stack-Plan.md 4.2 and
# 9), and the portable-asan preset compiles the same sources on Linux, where rawpage_ui,
# rawpage_platform and PDFium do not exist at all. If the boundary leaks, it leaks QUIETLY, months
# before anyone notices; this is what makes it fail loudly at configure time instead.
#
# It checks the RESOLVED LINK CLOSURE, not just what tests/unit/CMakeLists.txt writes: a transitive
# edge -- rawpage_cos gaining a dependency on rawpage_platform, say -- is exactly the way this leaks
# without anybody editing the file that states the rule.
#
# --------------------------------------------------------------------------------------
# WHY IT LIVES IN cmake/ RATHER THAN INSIDE tests/unit/CMakeLists.txt (Stage 2 T1)
# --------------------------------------------------------------------------------------
# Because tests/gates/ proves it fires, and a fixture that carried its own copy of the function
# would prove that the COPY fires. That is convention 7's rule -- a control that does not exercise
# the thing is worse than none -- and Stage 0's own I-15 is the same lesson from the other side.
# The fixture at tests/gates/fixtures/headless-assert/ includes THIS file.
#
# --------------------------------------------------------------------------------------
# WHY rawpage_pdfium IS ON THE LIST (decision D2-2, Stage2-Rendering-Plan.md 3)
# --------------------------------------------------------------------------------------
# It is the load-bearing half of the pdf/ library split, and without it the split is decorative: the
# forbidden list named only rawpage_ui and rawpage_platform, so the moment PdfiumInit.cpp joined a
# library tests/unit links, the unit suite would have linked a PDF engine while this assertion --
# whose entire purpose is to prevent that -- kept passing.
#
# THE NAME IS CHECKED BEFORE THE TARGET IS RESOLVED, and that ordering is deliberate. rawpage_pdfium
# is Windows-only and does not exist on the portable presets at all; a check that skipped names
# which are not targets would therefore be silent on precisely the configuration whose portability
# it exists to defend, and the developer would get CMake's own "target not found" from the generate
# step instead of the diagnostic below. A forbidden name is forbidden whether or not something in
# this configuration happens to define it.

function(rawpage_assert_headless target)
    set(forbidden rawpage_ui rawpage_platform rawpage_pdfium)
    set(pending ${target})
    set(seen "")

    while(pending)
        list(POP_FRONT pending current)
        if(current IN_LIST seen)
            continue()
        endif()
        list(APPEND seen ${current})

        foreach(bad IN LISTS forbidden)
            if(current STREQUAL bad)
                message(FATAL_ERROR
                    "\n${target} links ${bad}, directly or transitively.\n\n"
                    "The unit suite must build and pass with NO DISPLAY and NO PDF ENGINE. "
                    "That is not a preference: core/ is required to be testable in isolation "
                    "(Tech-Stack-Plan.md 4.2, 9), and the portable-asan preset compiles the "
                    "same sources on Linux where none of those libraries exists at all.\n\n"
                    "If a test needs a widget or an OS handle, the thing it is testing "
                    "belongs behind an interface, not in this suite. If it needs to RASTERIZE, "
                    "it belongs in rawpage_render_tests -- adding the engine here is the "
                    "pressure decision D2-2 exists to resist.\n")
            endif()
        endforeach()

        if(NOT TARGET ${current})
            continue()
        endif()

        # BOTH properties, and the second is not padding. `LINK_LIBRARIES` holds what a static
        # library or an executable links; an INTERFACE library and an IMPORTED target -- Qt's
        # `Qt6::Core` among them -- carry their dependencies in `INTERFACE_LINK_LIBRARIES` and leave
        # the first NOTFOUND. Reading only one of the two makes the walk stop at the first edge of
        # the wrong kind and report a clean closure it never finished traversing, which is a worse
        # failure than no check: it is a check that says yes.
        set(deps "")
        get_target_property(direct_deps ${current} LINK_LIBRARIES)
        if(direct_deps)
            list(APPEND deps ${direct_deps})
        endif()
        get_target_property(interface_deps ${current} INTERFACE_LINK_LIBRARIES)
        if(interface_deps)
            list(APPEND deps ${interface_deps})
        endif()

        if(deps)
            # EVERY dependency name is queued, not only the ones that resolve to a target here.
            # Filtering to targets at this point would undo the ordering above: rawpage_pdfium named
            # by a library that does not exist on this preset is exactly the case the list is for,
            # and it would be dropped before it was ever compared. Names that are not targets are
            # skipped at the top of the loop, after the check, so a linker flag or an imported
            # library costs one string comparison and nothing else.
            foreach(dep IN LISTS deps)
                list(APPEND pending ${dep})
            endforeach()
        endif()
    endwhile()
endfunction()
