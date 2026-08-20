// Stage 0 placeholder translation unit.
//
// It exists so rawpage_ui is a non-empty archive and so the target graph, link order,
// and warning flags are exercised from the first commit rather than from the first
// real code. app/main.cpp calls the anchor below, which makes the link genuinely
// resolve a symbol out of this library instead of merely listing it.
//
// Replaced by real content at Stage 1 (MainWindow, Ribbon, the shell). Delete this file then -- a placeholder that
// outlives its purpose becomes a mystery.

namespace rawpage::ui {

void moduleAnchor()
{
}

} // namespace rawpage::ui
