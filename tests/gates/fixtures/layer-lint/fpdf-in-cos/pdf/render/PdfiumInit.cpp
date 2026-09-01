// Legitimate, and present so the fixture's failure is attributable to WHICH directory named the
// engine rather than to PDFium appearing anywhere in the tree.
namespace rawpage::pdf::render { void ensure() { FPDF_InitLibrary(); } }
