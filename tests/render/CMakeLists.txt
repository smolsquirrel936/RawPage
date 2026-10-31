# tests/render -- the suite that links the ENGINE. Stage 2 T2, plan §5.1 and §5.6.
#
# WHY IT IS A SEPARATE TARGET, which is the same argument for the third time and the reason it is
# worth restating rather than cross-referencing. tests/unit links rawpage_core and rawpage_cos and
# nothing else, and rawpage_assert_headless() enforces that against the resolved closure -- the
# standing proof that core/ is testable with no display and no engine, which portable-asan verifies
# on a machine that has neither. The RASTER suite is precisely the pressure that would otherwise
# "just add rawpage_pdfium" to that target and destroy the property while every test still passed.
# tests/ui met the same pressure from the widget side at Stage 1 T9 and tests/platform from the OS
# side at T7; D2-2 named this one in advance, which is why it arrives as a target rather than as a
# link line.
#
# WINDOWS ONLY, because it rasterizes and PDFium is a Windows DLL here. It needs no display: nothing
# in this target is a widget, and case A-07's claim -- that initializing the engine does not require
# one -- is a thing this suite establishes rather than assumes, by running in a process that has no
# QGuiApplication at all.
#
# THE `render.` PREFIX IS NOT YET ASSERTED BY CI, and this comment claimed the opposite until it was
# checked at T16. `.github/workflows/ci.yml`'s non-empty-prefix loop names `unit`, `ui`, `platform`
# and `bench`; `render` was never added when this target arrived at T2, so a target that failed to
# build would subtract 40 cases from a run still reporting "100% tests passed" -- which is the exact
# failure that loop exists to catch, in the one suite that links the engine.
#
# It is a one-word fix and it belongs to **T18**, which owns this stage's CI work (see
# `Stage2-Rendering-Plan.md` 10, where the same gap is recorded against the summary job's prefix
# list). Written here as well as there because this is the file whose absence would be missed.

find_package(Catch2 3 REQUIRED)

add_executable(rawpage_render_tests
    tst_pdfiuminit.cpp            # Stage 2 T2 -- guide cases A-07's engine half, and A-45's first
    tst_pdfiumdoc.cpp             # Stage 2 T9 -- guide cases A-42 and A-43
    tst_docmodels.cpp             # Stage 4 T4 -- guide cases A-25 to A-28 and A-30: what the
                                  #   facade actually reads out of a file for core/doc/Outline,
                                  #   Destinations and Metadata
    tst_contentbox.cpp            # Stage 4 T12 -- guide cases A-11, A-67 and A-68: the union of a
                                  #   page's object bounds through the facade, and the three zooms
                                  #   that close Stage 2's I-28. It is HERE because the observable
                                  #   is a number read off a real file and this is the only target
                                  #   that can open one -- which is why D4-14 moved the fit
                                  #   arithmetic into core/render/, where no widget is needed.
    tst_pageopsengine.cpp         # Stage 3 T3 -- guide case A-29: pdf/ops/PageOps against the engine,
                                  #   page by page, over every corpus file both readers open
    tst_backendedit.cpp           # Stage 3 T4 -- guide cases A-36's corpus half, A-37, A-34's engine half
    tst_rasterizer.cpp            # Stage 2 T9 -- guide cases A-44, A-45, A-46's second half,
                                  #               and A-47's engine half
    tst_fontbridge.cpp            # Stage 2 T16 -- guide cases A-66 and A-67. The ONLY suite that
                                  #   drives PDFium -> FontResolverBridge -> WinFontResolver ->
                                  #   core/render/FontSubstitution end to end, for the reason
                                  #   below about rawpage_platform.
    tst_renderpipeline.cpp        # Stage 2 T13 -- the REAL engine on the REAL scheduler, through
                                  #   app/BackendRasterizer. Every other suite fakes one end of
                                  #   this: tests/unit and tests/ui fake the rasterizer (they may
                                  #   not link an engine), and the other cases here call the
                                  #   rasterizer with no scheduler. The assembly the product
                                  #   actually runs had no case until this file.
    tst_extract.cpp               # Stage 5 T2 -- guide cases A-17 to A-26: pdf/render/PdfiumTextExtractor
                                  #   over the corpus, graded against the GENERATOR's labels in
                                  #   corpus/labels/ and never against PDFium's own FPDFText_GetText,
                                  #   and through the real scheduler via app/BackendTextExtractor
    tst_layoutcorpus.cpp          # Stage 5 T4 -- guide A-38 (D5-7): LayoutAnalyzer against the
                                  #   generator's labels, graded on the guide's section 3.2 targets;
                                  #   and the corpus halves of A-23, A-32, A-34, A-36 and A-39
    tst_searchcorpus.cpp          # Stage 5 T6 -- guide A-63 (the guide's 3.3, over text-900.pdf), A-58,
                                  #   A-59's and A-21's corpus halves, A-24's search lane: the real engine
                                  #   on the shipped scheduler, DocumentIndex and SearchEngine
    tst_foldersearch.cpp          # Stage 5 T9 -- guide A-80, A-81, A-82, A-88 (D5-13): core/text/FolderSearch
                                  #   over a scratch copy of the corpus through the shipped backend's staged
                                  #   open, and the 900-page open's longest engine piece
    tst_comparecorpus.cpp         # Stage 5 T12 -- guide A-95 ... A-102 (D5-17): core/text/DocumentComparer
                                  #   over corpus/compare/, graded against each pair's expected.json, on
                                  #   the shipped backend's extracted text and 72-dpi rasters
    tst_searchclasses.cpp         # Stage 5 T8 -- guide A-76, A-78, A-79 (D5-12): search-classes.pdf's four
                                  #   other content classes through the shipped backend's searchUnits,
                                  #   graded against the generator's label
    tst_annotreader.cpp           # Stage 6 T2 -- guide A-17 ... A-19 (D6-3): pdf/ops/AnnotationReader through the
                                  #   shipped backend, against the generator's labels and the engine, page by page
    tst_annotdisplay.cpp          # Stage 6 T4 -- guide A-34 (through commands), A-35, A-36, A-37, A-39 (D6-2,
                                  #   D6-10): what the live engine draws after an edit is what the saved file
                                  #   draws; the reload on the render thread, between jobs, with no copy
    tst_typedtext.cpp             # Stage 6 T7 -- guide A-56's engine half (D6-7): every character the CJK
                                  #   coverage table claims read back and drawn by PDFium in its face, and
                                  #   Helvetica's advance table held to the engine's own metrics
    tst_appearance.cpp            # Stage 6 T8 -- guide A-65 (D6-6): our appearance against the one PDFium
                                  #   generates for the same dictionary with no /AP, ink IoU >= 0.90
    tst_attachments.cpp           # Stage 6 T11 -- guide A-82, A-84, A-85's backend half (D6-17): the shipped
                                  #   backend's listing held to the label and to the engine, an extract that writes
                                  #   its bytes and nothing else, an attached PDF opened from its bytes read-only
    tst_measuregeometry.cpp       # Stage 6 T12 -- guide A-89's engine half and A-91 (D6-14): a page's paths read
                                  #   by PDFium against the generator's segments, a form's matrix and a curve's
                                  #   anchors, the 100,000-segment bound, and the read as an engine task, once
    tst_assemblylabels.cpp        # Stage 7 T1 -- guide A-15's engine half (D7-17, D7-18): every identity page of
                                  #   corpus/assembly/ shows PDFium the identity its label gives, the outline and named
                                  #   destinations land on the labelled pages, and Stage 7 guide I-01's regression
    tst_reference_repair.cpp      # Stage 7 T2 -- guide A-19, A-20, A-21 (D7-2, D7-18): every page command over every
                                  #   fixture with references, SAVED and reopened, each reference graded by the identity
                                  #   PDFium extracts from the page it lands on; each page deleted in turn; the labels
    tst_copierengine.cpp          # Stage 7 T5 -- guide A-38 (D7-4): every page of every editable corpus file copied and
                                  #   inserted, saved, reopened and drawn against its source, 0 px at 72 and 144 dpi
    tst_remap.cpp                 # Stage 7 T3 -- guide A-30, A-32 (D7-3): a move of pages-900.pdf's pages 10 ... 19
                                  #   served wholly from the cache at their new indices, the engine never entered for
                                  #   one, each graded by its text and the saved file's pixels; an unsaved rotation
                                  #   turned once through a move and each undo
    tst_composegrowth.cpp         # Stage 7 T4 -- guide A-35, A-36, A-37 (Q4, DW-4's second cause): what a page command
                                  #   builds after 3,000 unsaved objects is what it changed; a move its verify refuses
                                  #   after the run returned is taken back and never drawn; commands, a tile and a save
                                  #   wait for a verify in flight
    tst_migratecorpus.cpp        # Stage 6 T13 -- guide A-98, A-99 (D6-21): annots-report-v1.pdf's comments carried
                                  #   to compare/report-v2.pdf over the engine's text and Stage 5's alignment, graded on
                                  #   §3.5 against labels/annots-migration.json
    # The appearance cases' spec and page builder (tests/golden/AppearanceCases), compiled in -- the golden
    # suite and golden-update read the same one.
    ${CMAKE_SOURCE_DIR}/tests/golden/AppearanceCases.cpp
    tst_saveengine.cpp           # Stage 3 T8 -- guide cases A-54's real half, A-57's writing half, A-58:
                                  #   the shipped backend, the real WinDurableFiles, PDFium re-reading
                                  #   the saved file and drawing it as the unsaved delta drew it
    # Stage 5 T12: A-97's bound is LIVE heap (Stage 2 I-35), and this walk of the heaps is the bench's
    # measure of it -- one source, so the case and the benches cannot measure two different things.
    ${CMAKE_SOURCE_DIR}/tests/bench/HeapCensus.cpp
)

# The corpus, by absolute source path -- the same mechanism tests/unit uses and for the same
# reason: a test that guessed a relative path would pass or fail on whatever working directory
# CTest happened to use.
target_compile_definitions(rawpage_render_tests PRIVATE
    RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus"
    # Stage 3 T8: where A-57 saves the four signed files for OpenSSL and for M-03's product walk. The
    # BUILD tree, never the source tree -- they are measurements, regenerated every run.
    RAWPAGE_A4_PRODUCT_DIR="${CMAKE_BINARY_DIR}/a4-product"
    # Stage 6 T3 (guide A-33): the same four files with a COMMENT added instead of a rotation -- the
    # permitted edit Stage 3 carried here (its guide's I-06) -- for OpenSSL and for M-03's Acrobat walk.
    RAWPAGE_A4_ANNOT_DIR="${CMAKE_BINARY_DIR}/a4-product-annot"
    # Stage 7 T2 (S7 guide A-27): the same four files after PAGE commands -- duplicated, moved and deleted,
    # saved after each -- for OpenSSL (D7-20: a signed document is edited, and its signed bytes keep).
    RAWPAGE_A4_PAGES_DIR="${CMAKE_BINARY_DIR}/a4-product-pages")

target_include_directories(rawpage_render_tests PRIVATE "${CMAKE_SOURCE_DIR}")

# rawpage_platform JOINED AT T16, and it is worth saying why rather than letting a link line grow.
#
# The font chain has three links in three libraries -- PDFium, pdf/render/FontResolverBridge and
# platform/win/WinFontResolver -- and no existing target could hold more than one of them:
# tests/unit may link neither the engine nor the OS layer (rawpage_assert_headless(), D2-2),
# tests/platform may not link the engine, and this target had no OS layer. So the arrangement the
# product actually runs was, once again, the one arrangement no suite could reach. That is issue
# I-09 exactly -- the finding that made app/BackendRasterizer a header at T13 -- and the answer is
# the same one: give the suite the real assembly rather than a copy of it.
#
# It does NOT weaken what this target's separateness protects. The property being protected is that
# tests/unit links no engine and no display; adding the OS layer to a Windows-only, engine-linking
# target costs nothing of that. Both of these libraries are already in the shipped executable.
# rawpage_ops JOINED AT Stage 3 T3 for tst_pageopsengine: the page walk has to be held to the engine's
# page order, and this is the only target that links both. It adds no engine to anything -- rawpage_ops
# is portable and links none (D3-3).
target_link_libraries(rawpage_render_tests PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_ops
    rawpage_pdfium
    rawpage_platform
    Catch2::Catch2WithMain
)

rawpage_common_flags(rawpage_render_tests)

# --- A-57: risk A4 through the product, judged by OpenSSL ------------------------------------------
# tests/unit's unit.a4_probe.openssl shape (RunA4Probe.cmake) over D3-1's SECOND half: the shipped
# save path writes, then CheckSignatures.cmake judges the files on disk, controls first. A missing
# OpenSSL or Python FAILS it. The verdict is the exit code -- no PASS_REGULAR_EXPRESSION.
add_test(NAME render.a4_product.openssl
    COMMAND ${CMAKE_COMMAND}
            "-DTEST_BINARY=$<TARGET_FILE:rawpage_render_tests>"
            "-DPRODUCT_DIR=${CMAKE_BINARY_DIR}/a4-product"
            "-DCHECKS_DIR=${CMAKE_SOURCE_DIR}/tests/gates"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunA4Product.cmake")
set_tests_properties(render.a4_product.openssl PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin;PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# --- A-33 (Stage 6 T3): A4 through the product with the PERMITTED edit, judged by OpenSSL ------------
# The same runner over the case that adds a highlight through AddAnnotationCommand and saves through
# SaveCoordinator. `-R a4_product` selects both, as the Stage 6 guide's A-33 says.
add_test(NAME render.a4_product.annotation.openssl
    COMMAND ${CMAKE_COMMAND}
            "-DTEST_BINARY=$<TARGET_FILE:rawpage_render_tests>"
            "-DPRODUCT_DIR=${CMAKE_BINARY_DIR}/a4-product-annot"
            "-DCHECKS_DIR=${CMAKE_SOURCE_DIR}/tests/gates"
            "-DCASE=A4 product saves an annotation into every signed corpus file"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunA4Product.cmake")
set_tests_properties(render.a4_product.annotation.openssl PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin;PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
# --- Stage 7 T2 (S7 guide A-27): the same runner over PAGE commands -- a duplicate, a move and a delete, each
# saved -- on the four signed files. `-R a4_product` selects all three.
add_test(NAME render.a4_product.pages.openssl
    COMMAND ${CMAKE_COMMAND}
            "-DTEST_BINARY=$<TARGET_FILE:rawpage_render_tests>"
            "-DPRODUCT_DIR=${CMAKE_BINARY_DIR}/a4-product-pages"
            "-DCHECKS_DIR=${CMAKE_SOURCE_DIR}/tests/gates"
            "-DCASE=A4 product saves page commands into every signed corpus file"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunA4Product.cmake")
set_tests_properties(render.a4_product.pages.openssl PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${RAWPAGE_MINGW_ROOT}/bin;PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
# All alone under ctest -j: each re-runs a Catch2 case that ctest also runs as a test of its own, and
# the two write the same files in one directory -- one could delete or rewrite a file while OpenSSL
# reads the other's (cmake/RawPageSerialTests.cmake).
set_tests_properties(render.a4_product.openssl render.a4_product.annotation.openssl render.a4_product.pages.openssl
                     PROPERTIES RUN_SERIAL TRUE)

include(Catch)
catch_discover_tests(rawpage_render_tests
    TEST_PREFIX "render."
    # Two directories, and the second is the new one: libstdc++/libgcc as everywhere else, plus the
    # output directory, because pdfium.dll is deployed there rather than being on any system path.
    # Without it every case in this suite fails to LAUNCH with 0xC0000135, which looks nothing like
    # a test failure.
    DL_PATHS "${RAWPAGE_MINGW_ROOT}/bin" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
rawpage_serial_tests(rawpage_render_tests PREFIX "render."
    DL_PATHS "${RAWPAGE_MINGW_ROOT}/bin" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
