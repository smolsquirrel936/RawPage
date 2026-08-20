// The waiver syntax QUOTED, not applied. Both shapes below came from layer-lint's own source,
// where they were being counted as active waivers (I-20): the census read 3 with zero real
// waivers in the tree.
//
//   // layer-lint: allow-filesystem-write(reason)
#include <fstream>

namespace rawpage::ui {
// Code on this line, but the marker sits behind a second // -- it is being described, not
// applied, so the write below must still be reported.
void offend() { std::ofstream out("notes.txt"); }  // see // layer-lint: allow-filesystem-write(reason)
}
