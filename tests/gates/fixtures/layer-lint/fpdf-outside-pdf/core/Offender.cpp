// LL-3: the PDFium C API named outside pdf/.
namespace recto::core {
void offend() { FPDF_InitLibrary(); }
}
