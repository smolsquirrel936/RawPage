// LL-4: a widget writing to disk directly.
#include <fstream>
namespace rawpage::ui {
void offend() { std::ofstream out("notes.txt"); }
}
