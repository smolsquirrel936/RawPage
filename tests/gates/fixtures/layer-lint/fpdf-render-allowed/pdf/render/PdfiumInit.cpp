// FIXTURE, NEGATIVE CONTROL -- the facade doing its only job. Without this case, "FPDF_ outside
// pdf/render/ fails" is equally satisfied by a rule that rejects PDFium everywhere, which would
// make pdf/render/ impossible to write.
namespace rawpage::pdf::render {
void ensure()
{
    FPDF_InitLibrary();
    FPDFPage_GenerateContent(nullptr);
    FPDFText_GetText(nullptr, 0, 0, nullptr);
    FPDFBitmap_Destroy(nullptr);
}
}
