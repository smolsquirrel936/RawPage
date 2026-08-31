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
# `rawpage_pdfium` IS NOT DECLARED HERE YET, and that is deliberate rather than an omission: it has
# no sources until T2 writes pdf/render/PdfiumInit, and a static library declared with a placeholder
# TU to keep CMake happy is precisely the thing this task just deleted. The guarantee does not wait
# for the target -- rawpage_assert_headless() rejects the NAME whether or not it resolves to
# something, which is what makes the rule hold on the portable presets where the target will never
# exist at all.

add_library(rawpage_cos STATIC
    # PdfModule.cpp -- Stage 0's placeholder translation unit -- was DELETED at Stage 2 T1, as its
    # own comment instructed and as Stage1-AppShell-Plan.md 10 recorded it would be. It existed so
    # this archive was non-empty and the link order was exercised from the first commit; the factory
    # below is real content, and a placeholder that outlives its purpose becomes a mystery.
    BackendFactory.h
    BackendFactory.cpp
)

target_include_directories(rawpage_cos PUBLIC "${CMAKE_SOURCE_DIR}")

target_link_libraries(rawpage_cos PUBLIC rawpage_core)

rawpage_strict(rawpage_cos)           # -Werror: no warning survives in pdf/ either
