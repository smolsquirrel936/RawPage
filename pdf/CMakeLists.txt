# pdf/ -- the PDF engine layer.
#
# The ONLY layer that may emit PDF bytes or name an FPDF_ symbol. tools/layer-lint
# fails the build on either appearing anywhere else.
#
#   pdf/cos/     first-party: lexer, object model, xref, filters, crypt, incremental
#                writer. Portable C++ by rule -- it is half of what portable-asan and
#                portable-fuzz build on Linux.
#   pdf/render/  the PDFium facade. Nothing but buffers and callbacks crosses that
#                boundary: no C++ object, no exception, no heap ownership.
#   pdf/ops/     engine-side realization of each mutation.
#
# In one line: we read through PDFium and write through our own code.

add_library(rawpage_pdf STATIC
    PdfModule.cpp
)

target_include_directories(rawpage_pdf PUBLIC "${CMAKE_SOURCE_DIR}")

target_link_libraries(rawpage_pdf PUBLIC rawpage_core)

rawpage_strict(rawpage_pdf)           # -Werror: no warning survives in pdf/ either
