// THROWAWAY. This program exists to answer one question and then be deleted.
//
// RISK A6 (Tech-Stack-Plan.md 3.3): *can PDFium be consumed from a GCC-built binary?*
// The reasoning says yes -- FPDF_* is a C API, x64 Windows has a single calling
// convention, and the facade in pdf/render/ passes no C++ object, no exception, and no
// heap ownership across the boundary. But the reasoning has never been compiled.
//
// What this proves, and nothing more:
//   1. a prebuilt PDFium DLL links from g++,
//   2. its C API can be called and returns sane answers,
//   3. the resulting binary imports msvcrt.dll, not ucrtbase.dll -- i.e. no second C
//      runtime arrives with PDFium.
//
// What it deliberately does NOT do: render, extract text, or wrap anything. Stage 2 builds
// pdf/render/PdfiumInit and PdfiumDocument properly, behind the facade. The durable output
// of this task is the recorded verdict plus scripts/fetch-pdfium.ps1, not this file.

#include <fpdfview.h>

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: spike-pdfium <file.pdf>\n");
        return 2;
    }

    FPDF_LIBRARY_CONFIG config{};
    config.version = 2;
    config.m_pUserFontPaths = nullptr;
    config.m_pIsolate = nullptr;
    config.m_v8EmbedderSlot = 0;
    FPDF_InitLibraryWithConfig(&config);

    FPDF_DOCUMENT document = FPDF_LoadDocument(argv[1], /*password=*/nullptr);
    if (document == nullptr) {
        // The error code crosses the boundary as a plain unsigned long -- which is the
        // whole point: nothing richer than a scalar needs to survive the ABI.
        std::fprintf(stderr, "FPDF_LoadDocument failed, FPDF_GetLastError=%lu\n",
                     FPDF_GetLastError());
        FPDF_DestroyLibrary();
        return 1;
    }

    const int pageCount = FPDF_GetPageCount(document);
    std::printf("pages=%d\n", pageCount);

    // One page-level call as well, so the answer does not rest on a single entry point.
    double width = 0.0;
    double height = 0.0;
    const int sizeOk = FPDF_GetPageSizeByIndex(document, 0, &width, &height);
    std::printf("page0=%s %.3f x %.3f pt\n", sizeOk ? "ok" : "FAILED", width, height);

    FPDF_CloseDocument(document);
    FPDF_DestroyLibrary();

    if (pageCount <= 0 || !sizeOk) {
        std::fprintf(stderr, "spike-pdfium: implausible results\n");
        return 1;
    }

    std::printf("A6: a PDFium DLL links from g++ and opens a document.\n");
    return 0;
}
