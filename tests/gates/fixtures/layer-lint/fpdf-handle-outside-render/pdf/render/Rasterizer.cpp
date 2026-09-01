// Legitimate: the facade holds handles, which is the whole reason it is a facade.
namespace rawpage::pdf::render { FPDF_DOCUMENT open() { return nullptr; } }
