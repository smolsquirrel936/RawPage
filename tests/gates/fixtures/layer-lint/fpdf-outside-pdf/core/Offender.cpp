// LL-3: the PDFium C API named outside pdf/.
namespace rawpage::core {
void offend() { FPDF_InitLibrary(); }
}
