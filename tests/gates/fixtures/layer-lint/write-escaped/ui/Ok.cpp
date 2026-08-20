// LL-5: negative control for the escape hatch.
#include <fstream>
namespace rawpage::ui {
void ok() { std::ofstream out("x"); } // layer-lint: allow-filesystem-write(fixture proving the waiver works)
}
