// A waiver naming a kind no rule declares waives nothing, so it must not suppress the write
// and must not appear in the census (I-20). "allow-<kind>" in RuleSet.h's prose was exactly
// this shape.
#include <fstream>

namespace rawpage::ui {
void offend() { std::ofstream out("notes.txt"); }  // layer-lint: allow-nonsense(not a real rule)
}
