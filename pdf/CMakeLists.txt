# pdf/ -- the PDF engine layer, and from Stage 2 T1 it is TWO libraries rather than one.
#
# The ONLY layer that may emit PDF bytes or name an FPDF_ symbol. tools/layer-lint fails the build
# on either appearing anywhere else.
#
#   pdf/cos/     first-party: lexer, object model, xref, filters, and (Stage 3) the incremental
#                writer. Portable C++ BY RULE -- it is half of what portable-asan and portable-fuzz
#                build on Linux.
#   pdf/render/  the PDFium facade. Nothing but buffers and callbacks crosses that boundary: no C++
#                object, no exception, no heap ownership.
#   pdf/ops/     engine-side realization of each mutation (Stage 3).
#
# In one line: we read through PDFium and write through our own code.
#
# --------------------------------------------------------------------------------------
# WHY THE SPLIT (decision D2-2, Stage2-Rendering-Plan.md 3)
# --------------------------------------------------------------------------------------
# `rawpage_pdf` was one library holding one placeholder translation unit, and tests/unit linked it
# under an assertion whose stated meaning is "must build and pass with NO DISPLAY AND NO PDF
# ENGINE". That was harmless while the library was empty and becomes FALSE the instant
# pdf/render/PdfiumInit.cpp joins it: the headless suite would link PDFium while the assertion that
# exists to prevent exactly that carried on passing. Worse, the root CMakeLists adds pdf/ on the
# portable presets too, where no Linux PDFium exists -- so portable-asan would stop CONFIGURING,
# which reads like infrastructure trouble rather than like a layering breach.
#
# So:
#
#   rawpage_cos      pdf/cos/ + pdf/BackendFactory   portable, first-party, NO PDFium
#   rawpage_pdfium   pdf/render/                     Windows only, links pdfium.dll   (arrives T2)
#
# The split is not tidiness. It is what makes the sentence in tests/unit's comment true, and what
# keeps pdf/cos/ genuinely portable rather than portable-by-assertion. THE LOAD-BEARING HALF IS THE
# FORBIDDEN-LIST ENTRY in cmake/RawPageHeadless.cmake -- a split without it just moves the leak one
# edge further away.
#
# `rawpage_pdfium` ARRIVED AT T2, at the bottom of this file, when pdf/render/PdfiumInit gave it a
# reason to exist. T1 deliberately did not declare it empty -- a static library kept alive by a
# placeholder TU is precisely what that task deleted -- and the guarantee never waited for the
# target: rawpage_assert_headless() rejects the NAME whether or not it resolves to something, which
# is what makes the rule hold on the portable presets where the target will never exist at all.

add_library(rawpage_cos STATIC
    # PdfModule.cpp -- Stage 0's placeholder translation unit -- was DELETED at Stage 2 T1, as its
    # own comment instructed and as Stage1-AppShell-Plan.md 10 recorded it would be. It existed so
    # this archive was non-empty and the link order was exercised from the first commit; the factory
    # below is real content, and a placeholder that outlives its purpose becomes a mystery.
    BackendFactory.h
    BackendFactory.cpp

    # The COS reader itself, from Stage 2 T5. Every file here is std-only on purpose: pdf/cos/ is
    # portable BY RULE, and the fuzz driver hands it a `const uint8_t*` that must not have to become
    # a Qt container once per iteration.
    cos/Lexer.h
    cos/Lexer.cpp

    # T6: the object model and the grammar over it. CosObject is the type Stage 3's byte-identical
    # save depends on -- it carries the exact span every object was written as, so an untouched
    # object is REWRITTEN BY COPYING rather than by re-serializing.
    cos/CosObject.h
    cos/CosObject.cpp
    cos/Parser.h
    cos/Parser.cpp
)

target_include_directories(rawpage_cos PUBLIC "${CMAKE_SOURCE_DIR}")

target_link_libraries(rawpage_cos PUBLIC rawpage_core)

rawpage_strict(rawpage_cos)           # -Werror: no warning survives in pdf/ either

# =======================================================================================
# rawpage_pdfium -- the PDFium facade. Stage 2 T2.
# =======================================================================================
# WINDOWS ONLY, and separate from rawpage_cos for the reason D2-2 gives at the top of this file:
# this is the library that links the engine, and tests/unit must never reach it. The guarantee is
# cmake/RawPageHeadless.cmake's, which has forbidden the NAME since T1 -- before the target existed,
# which is what makes the rule hold on the portable presets where it never will.
#
# It is not guarded by if(WIN32) here: the root CMakeLists only descends into pdf/ with
# RAWPAGE_PORTABLE_SUBSET off for the Windows presets, and adding a second, weaker guard beside a
# real one teaches a reader that neither is load-bearing. RAWPAGE_PDFIUM_VERSION is set by
# rawpage_assert_pdfium() in the root, and its absence below would be a configure error rather than
# a silently unbuilt engine.
if(NOT DEFINED RAWPAGE_PDFIUM_VERSION)
    message(FATAL_ERROR
        "\nrawpage_pdfium cannot be built: RAWPAGE_PDFIUM_VERSION is not set.\n\n"
        "The root CMakeLists calls rawpage_assert_pdfium() (cmake/PdfiumAssert.cmake) before adding "
        "this directory. Reaching this line means that call was removed or reordered -- which would "
        "leave the engine linked and its version unasserted, and D2-3 exists precisely to prevent "
        "that combination.\n")
endif()

# THE IMPORTED TARGET IS DECLARED HERE RATHER THAN THROUGH third_party/pdfium/PDFiumConfig.cmake,
# and that is a correctness point rather than a preference. The packager's config file sets
# IMPORTED_IMPLIB to `lib/pdfium.dll.lib`, which is MSVC format -- GNU ld cannot read it, and the
# failure is a wall of undefined FPDF_ references that says nothing about why. What we link is
# `lib/libpdfium.dll.a`, which scripts/fetch-pdfium.ps1 generates with gendef + dlltool from the
# pinned binutils. PA-5 has already asserted both files exist by the time this runs.
set(RAWPAGE_PDFIUM_DIR "${CMAKE_SOURCE_DIR}/third_party/pdfium")

add_library(pdfium SHARED IMPORTED GLOBAL)
set_target_properties(pdfium PROPERTIES
    IMPORTED_LOCATION             "${RAWPAGE_PDFIUM_DIR}/bin/pdfium.dll"
    IMPORTED_IMPLIB               "${RAWPAGE_PDFIUM_DIR}/lib/libpdfium.dll.a"
    INTERFACE_INCLUDE_DIRECTORIES "${RAWPAGE_PDFIUM_DIR}/include"
)

add_library(rawpage_pdfium STATIC
    render/PdfiumInit.h
    render/PdfiumInit.cpp
)

target_include_directories(rawpage_pdfium PUBLIC "${CMAKE_SOURCE_DIR}")

# The version reaches the code as a compile definition rather than through a generated header,
# because there is exactly one string and a header would be a build artifact to keep in step. It is
# PRIVATE: `render::version()` is the one accessor, so nothing else can drift from it.
target_compile_definitions(rawpage_pdfium PRIVATE
    RAWPAGE_PDFIUM_VERSION="${RAWPAGE_PDFIUM_VERSION}")

# core/ only, plus the engine. NOT rawpage_cos: the facade and the first-party COS reader are the
# two halves of D3 -- "we read through PDFium and write through our own code" -- and neither is
# built on the other. A dependency here would be the first step in the direction D2-2 split them to
# prevent.
target_link_libraries(rawpage_pdfium PUBLIC rawpage_core PRIVATE pdfium)

rawpage_strict(rawpage_pdfium)        # -Werror: pdf/ is a strict layer on both sides of the boundary

# --- The DLL beside the executable -----------------------------------------------------
# It is implicitly linked, so a missing pdfium.dll is not a lazy-load failure at first render -- the
# process does not start at all, with a loader error box and no log line. Copying it into the output
# directory at build time is what makes `ctest`, the bench and a hand-run rawpage.exe all work from a
# fresh build tree; Stage 15's windeployqt step packages the same file from the same place.
#
# A custom TARGET rather than a POST_BUILD on rawpage_pdfium: a static library's POST_BUILD does not
# run when only its consumers rebuild, and the copy is a dependency of every executable that links
# the engine rather than of the library itself.
add_custom_command(
    OUTPUT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/pdfium.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RAWPAGE_PDFIUM_DIR}/bin/pdfium.dll" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/pdfium.dll"
    DEPENDS "${RAWPAGE_PDFIUM_DIR}/bin/pdfium.dll"
    COMMENT "Deploying pdfium.dll beside the executable"
    VERBATIM)

add_custom_target(rawpage_pdfium_runtime DEPENDS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/pdfium.dll")
add_dependencies(rawpage_pdfium rawpage_pdfium_runtime)
