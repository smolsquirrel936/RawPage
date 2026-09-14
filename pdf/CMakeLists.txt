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
#   rawpage_ops      pdf/ops/                        portable, links rawpage_cos, NO PDFium  (Stage 3 T3, D3-3)
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
    # own comment instructed and as Stage1-AppShell-Plan.md 10 recorded it would be.
    #
    # BackendFactory.CPP MOVED TO pdf/render/ AT T9, and only the header stays here. A backend that
    # opens documents IS the engine, so the definition had to leave this library: tests/unit links
    # rawpage_cos under rawpage_assert_headless(), which forbids rawpage_pdfium BY NAME (D2-2). The
    # header is still listed because pdf/BackendFactory.h is what app/CompositionRoot includes, and
    # a header nobody lists is one an IDE cannot find.
    BackendFactory.h

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

    # T7: the nine stream filters (D2-4 -- NINE, not ten; CryptFilter is Stage 10's, with the
    # StandardSecurityHandler whose key it needs). Filter.{h,cpp} is the registry, the DecodeParms
    # and the predictor that Flate and LZW share -- it is not a tenth filter, and guide case A-19
    # asserts the filter LIST rather than counting files in this directory.
    cos/filters/Filter.h
    cos/filters/Filter.cpp
    cos/filters/AsciiHexFilter.h
    cos/filters/AsciiHexFilter.cpp
    cos/filters/Ascii85Filter.h
    cos/filters/Ascii85Filter.cpp
    cos/filters/RunLengthFilter.h
    cos/filters/RunLengthFilter.cpp
    cos/filters/LZWFilter.h
    cos/filters/LZWFilter.cpp
    cos/filters/FlateFilter.h
    cos/filters/FlateFilter.cpp
    cos/filters/CcittFilter.h
    cos/filters/CcittFilter.cpp
    cos/filters/DctFilter.h
    cos/filters/DctFilter.cpp
    cos/filters/JpxFilter.h
    cos/filters/JpxFilter.cpp
    cos/filters/Jbig2Filter.h
    cos/filters/Jbig2Filter.cpp

    # T7: object numbers to byte offsets. Classic tables, cross-reference STREAMS, and the /Prev
    # chain across incremental updates.
    cos/XrefTable.h
    cos/XrefTable.cpp

    # T8: why damaged files open instead of failing. The Stage 2 exit criterion -- every
    # corpus/malformed/ file yields a report and never a crash -- is this file's.
    cos/XrefRepair.h
    cos/XrefRepair.cpp

    # Stage 3 T2: the WRITER. Here rather than in pdf/ops/ because it is COS syntax and offsets with
    # no opinion about what an edit means, and because this library is what portable-asan builds --
    # the incremental writer is the memory-unsafe code Stage 3 adds, and D3-3 exists so that it runs
    # under a sanitizer. It returns the update and never holds a writable copy of the original.
    cos/ObjectAllocator.h
    cos/ObjectAllocator.cpp
    cos/IncrementalWriter.h
    cos/IncrementalWriter.cpp
)

target_include_directories(rawpage_cos PUBLIC "${CMAKE_SOURCE_DIR}")

# --- The codecs (Stage 2 T7) -----------------------------------------------------------
# vcpkg's `codecs` feature, declared at Stage 0 (D0-3) and dry-run-verified at precondition P-3,
# is switched on by CMakePresets' VCPKG_MANIFEST_FEATURES for the Windows AND portable presets.
# T7 is where it stops being theoretical.
#
# WHY THESE THREE AND NOT MORE. zlib is not an image dependency here -- it is the reason the reader
# can open a modern document at all, since every PDF 1.5-or-later file keeps its cross-reference
# table and its object streams Flate-compressed. libjpeg-turbo and openjpeg back /DCTDecode and
# /JPXDecode. CCITT is first-party (see CcittFilter.h: libtiff's fax decoder is reachable only
# through a TIFF* handle, so borrowing it costs more than writing it), and JBIG2 has no permissible
# library at all -- jbig2dec is AGPL-3.0, which D4 forbids outright (see Jbig2Filter.h and I-06).
#
# PUBLIC vs PRIVATE: PRIVATE, because none of the three appears in a pdf/cos/ header. A caller of
# `decodeStream()` gets a std::vector<uint8_t> and never learns which library produced it, which is
# what keeps `tests/unit` from acquiring a transitive jpeglib.h include.
find_package(ZLIB REQUIRED)
find_package(JPEG REQUIRED)
find_package(OpenJPEG CONFIG REQUIRED)

target_link_libraries(rawpage_cos PUBLIC rawpage_core PRIVATE ZLIB::ZLIB JPEG::JPEG openjp2)

rawpage_strict(rawpage_cos)           # -Werror: no warning survives in pdf/ either

# --- pdf/ops/ (Stage 3 T3, D3-3) -------------------------------------------------------
# rawpage_ops: portable, links rawpage_cos and no engine, and is added HERE -- above the return
# below -- so the portable presets build it and its code runs under ASan. pdf/ops/CMakeLists.txt says
# why it is not part of rawpage_pdfium.
add_subdirectory(ops)

# --- THE PORTABLE SUBSET STOPS HERE ----------------------------------------------------
# The portable presets (`portable-asan`, `portable-fuzz`) build core/ + pdf/cos/ on Linux, and
# pdf/cos/ is everything above this line. Everything below links PDFium, which does not exist on
# that platform: the root CMakeLists deliberately skips rawpage_assert_pdfium() for these two
# configurations, so RAWPAGE_PDFIUM_VERSION is unset by design rather than by accident.
#
# This guard was MISSING from T2 (2026-09-01) until 2026-09-06, and the comment that stood here
# claimed the root "only descends into pdf/ with RAWPAGE_PORTABLE_SUBSET off" -- which was never
# true: the root must descend, because the portable subset needs rawpage_cos. So `portable-asan`
# and `portable-fuzz` failed to CONFIGURE for the whole of Stage 2, hitting the FATAL_ERROR below
# that exists to catch a reordered assert. It went unseen because no CI run happened between
# Stage 1's sign-off and T18. A comment asserting a guard that lives in another file is a claim
# nothing re-derives -- convention 8's shape, and the reason this one is a `return()` you can see.
if(RAWPAGE_PORTABLE_SUBSET)
    message(STATUS "pdf/: portable subset -- rawpage_cos and rawpage_ops, no engine facade")
    return()
endif()

# =======================================================================================
# rawpage_pdfium -- the PDFium facade. Stage 2 T2.
# =======================================================================================
# WINDOWS ONLY, and separate from rawpage_cos for the reason D2-2 gives at the top of this file:
# this is the library that links the engine, and tests/unit must never reach it. The guarantee is
# cmake/RawPageHeadless.cmake's, which has forbidden the NAME since T1 -- before the target existed,
# which is what makes the rule hold on the portable presets where it never will.
#
# RAWPAGE_PDFIUM_VERSION is set by rawpage_assert_pdfium() in the root. Past the `return()` above
# the only way to reach this line unset is for that call to have been removed or reordered on a
# Windows preset, which would leave the engine linked and its version unasserted -- so it stays a
# configure error rather than a silently unbuilt engine.
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

    # T9: a document opens, and a page is drawn. PdfiumDocument owns the FPDF_DOCUMENT and the
    # buffer PDFium points into; PdfiumRasterizer is the first real implementation of one of
    # core/backend.h's seven interfaces. Neither header names an engine type -- the document's
    # handle accessor is a void* on purpose, so including it does not drag fpdf_*.h into every
    # translation unit that wants a page count.
    render/PdfiumDocument.h
    render/PdfiumDocument.cpp
    render/PdfiumRasterizer.h
    render/PdfiumRasterizer.cpp

    # T16: font substitution on OUR side of the engine boundary. FontResolverBridge is the
    # FPDF_SYSFONTINFO hook -- PDFium asks, platform::IFontResolver answers, and every substitution
    # is logged and readable, which is the whole of guide case A-66. GlyphCache holds the face data
    # behind it; read its header before the name misleads you, because PDFium rasterizes and caches
    # the GLYPHS and this caches the FILES.
    render/GlyphCache.h
    render/GlyphCache.cpp
    render/FontResolverBridge.h
    render/FontResolverBridge.cpp

    # The backend itself, and the factory that vends it. Both are in THIS library rather than in
    # rawpage_cos for the reason at the top of the rawpage_cos block: they need the engine.
    DocumentBackendImpl.h
    DocumentBackendImpl.cpp
    render/BackendFactory.cpp
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
# rawpage_cos joins the link line at T9, and it is worth saying why that does NOT undo D2-2's
# split. The direction of the dependency is what matters: rawpage_pdfium may use the first-party
# COS reader -- PdfiumDocument runs it over the same bytes for the RepairReport that PDFium's own
# silent repair never produces -- while rawpage_cos still knows nothing about PDFium and still
# builds on the portable presets alone. The forbidden edge is the other way round, and
# rawpage_assert_headless() is what keeps it forbidden.
# rawpage_ops JOINS AT Stage 3 T4, in the same direction and for the same reason as rawpage_cos did at
# T9: the backend holds a first-party MutationSinkImpl per open document and DELEGATES every mutation to
# it (D3-3). The portable library knows nothing of PDFium; this one uses it.
target_link_libraries(rawpage_pdfium PUBLIC rawpage_core rawpage_cos rawpage_ops PRIVATE pdfium)

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
