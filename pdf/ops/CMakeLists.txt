# pdf/ops/ -- rawpage_ops, the engine-side realization of each mutation. Stage 3 T3, decision D3-3
# (claude-plan/Stage3-Mutation-Plan.md section 3).
#
# A PORTABLE library of its own, linking rawpage_cos and NO ENGINE -- and that is the decision, not
# a tidy-up. The page-object walk and the desired-state sink are the memory-unsafe code Stage 3 adds
# above the writer: offsets, object numbers from a damaged file, a tree that may loop. portable-asan
# and portable-fuzz are the only sanitizer coverage this project has (O10, Implementation-Plan.md
# section 8), so this library is added BEFORE pdf/CMakeLists.txt's portable-subset return and builds
# on Linux.
#
# The path of least resistance would have been to put pdf/ops/ beside pdf/DocumentBackendImpl in
# rawpage_pdfium, which is where the backend lives. That would have put Stage 3's riskiest code
# exactly where ASan cannot reach it. DocumentBackendImpl DELEGATES to this library instead (T4, T8).
#
# rawpage_tests links it under rawpage_assert_headless(), whose forbidden list is unchanged: the
# gate case gates.headless_assert.ops_carrying_pdfium_is_caught proves an engine arriving through
# this library would still be refused (guide case A-03). And tools/layer-lint's `pdf-cos` entry keeps
# the dependency one-way: pdf/cos/ may not include pdf/ops/ (guide case A-05).

add_library(rawpage_ops STATIC
    PageOps.h
    PageOps.cpp
    MutationSinkImpl.h
    MutationSinkImpl.cpp

    # Stage 3 T8 (D3-4): compose, write, re-read from the path, verify, adopt -- shared by both backends.
    RevisionWriter.h
    RevisionWriter.cpp

    # S4 T10 (D4-12). A READER in a directory named for operations, and the reason is that /Trans
    # is document semantics rather than object model: PageOps sits on the same line. It exists at
    # all because PDFium's C API exposes page transitions nowhere, which makes it the first read
    # in the project that D4-3's "read through PDFium" cannot cover.
    TransitionReader.h
    TransitionReader.cpp

    # S5 T8 (D5-12). The second such reader: an embedded file's /Desc, which PDFium's attachment API
    # does not read (it reads the embedded file's params dictionary instead). Names only to match.
    EmbeddedFileReader.h
    EmbeddedFileReader.cpp
    AttachmentFiles.h
    AttachmentFiles.cpp

    # S6 T2 (D6-3). The third: every page's /Annots, read first-party because editing needs each
    # annotation's object number and its dictionary as written, which PDFium's API does not give.
    AnnotationReader.h
    AnnotationReader.cpp

    # S6 T3 (D6-4, D6-5, D6-6): what an annotation intent means as COS objects -- a created
    # annotation's dictionary, an edited one's keys over the dictionary on disk, a page's /Annots --
    # and the /AP normal appearance RawPage draws for every type it creates (Highlight first).
    AnnotationOps.h
    AnnotationOps.cpp
    AppearanceStreamBuilder.h
    AppearanceStreamBuilder.cpp
)

target_include_directories(rawpage_ops PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(rawpage_ops PUBLIC rawpage_core rawpage_cos)

rawpage_strict(rawpage_ops)           # -Werror, like the rest of pdf/
