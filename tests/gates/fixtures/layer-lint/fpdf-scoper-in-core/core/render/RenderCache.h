// FIXTURE -- the spelling that slips past a search for `FPDF_`. PDFium ships include/cpp/, whose
// ScopedFPDFDocument and FPDFDocumentDeleter contain no underscore after FPDF, so the call rule
// never sees them; a core/ cache holding one is the same leak wearing the RAII wrapper.
#pragma once
#include <memory>

namespace rawpage::core::render {
class RenderCache {
public:
    ScopedFPDFDocument document;
    std::unique_ptr<void, FPDFPageDeleter> page;
};
}
