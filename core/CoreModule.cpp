// Stage 0 placeholder translation unit.
//
// It exists so rawpage_core is a non-empty archive and so the target graph, link order,
// and warning flags are exercised from the first commit rather than from the first
// real code. app/main.cpp calls the anchor below, which makes the link genuinely
// resolve a symbol out of this library instead of merely listing it.
//
// Replaced by real content at T10 (Result.h, Log.h, Geometry.h, Units.h). Delete this
// file then -- a placeholder that outlives its purpose becomes a mystery.

namespace rawpage::core {

void moduleAnchor()
{
}

} // namespace rawpage::core
