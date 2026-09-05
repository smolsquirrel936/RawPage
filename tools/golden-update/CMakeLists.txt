# tools/golden-update -- the producer of golden/, and the enforcement of D2-3's stated reason.
#
# NOT A GATE, and `checks` still runs three and says three. The three gates link nothing but the
# standard library so CI's `gates` job runs on a bare Linux runner with no Qt and no vcpkg (D0-1);
# this links Qt, PDFium and the platform layer, so it is excluded from those configurations
# explicitly rather than left to chance -- the same arrangement, and the same reason, as
# tools/ribbon-validate.
#
# Its standing is the one ribbon-validate has: a build-time producer rather than a rule. The RULE
# lives in tests/golden, which compares against what this writes, and one of those cases drives
# this executable to prove the --reason refusal actually happens (guide case A-50).
if(RAWPAGE_GATES_ONLY OR RAWPAGE_PORTABLE_SUBSET)
    return()
endif()

# Windows only, for the same reason tests/render is: it rasterizes, PDFium is a Windows DLL here,
# and platform/win/WinFontResolver is DirectWrite. A portable build has neither.
if(NOT WIN32)
    return()
endif()

add_executable(golden-update
    main.cpp
    # COMPILED IN, not linked -- the technique ribbon-validate uses for RibbonModel.cpp and
    # tests/unit for app/CommandLine.cpp. The suite and the tool must render, encode and compare
    # through the SAME code: a tool that wrote baselines the suite decoded differently would fail as
    # a rendering regression rather than as the disagreement between two programs that it is.
    #
    # The file lives under tests/ because that is the dependency direction
    # Architecture-FileMap.md 3.17 already declares -- tools/golden-update depends on tests/golden,
    # and nothing depends the other way.
    "${CMAKE_SOURCE_DIR}/tests/golden/GoldenSuite.cpp"
)

target_include_directories(golden-update PRIVATE "${CMAKE_SOURCE_DIR}")

target_compile_definitions(golden-update PRIVATE
    RAWPAGE_CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpus"
    RAWPAGE_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/golden")

target_link_libraries(golden-update PRIVATE
    rawpage_core
    rawpage_cos
    rawpage_pdfium
    rawpage_platform
)

# QtGui for the PNG codec. It is already in this project's Qt link closure through rawpage_ui, so
# tools/license-audit sees nothing new -- and Qt's PNG handler is built into QtGui rather than being
# an image-format plugin, which is why this works in a process with no display and no
# QGuiApplication. Through the wrapper, never Qt6::Gui directly (D0-5).
rawpage_link_qt(golden-update Core Gui)

rawpage_strict(golden-update)

# pdfium.dll is deployed beside the executables rather than being on any system path, and this tool
# is run by hand from a shell that has not been told about it. Nothing to do at configure time --
# the output directory is the same one the DLL lands in -- but say so, because a tool that fails to
# LAUNCH with 0xC0000135 looks nothing like a tool with a bug.
