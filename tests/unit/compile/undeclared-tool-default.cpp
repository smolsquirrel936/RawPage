// NEGATIVE CASE for guide case A-61 (Stage 6 T8, D6-13) -- a tool default `ToolDefaults.def` does not declare.
//
// The pencil has no font size: `core/settings/ToolDefaults.def` gives it no `FontSize` row. The family's
// guarantee is D1-10's for a row key -- a pair with no row is a NAME THAT DOES NOT EXIST, so the compiler is
// what reports it, not a setting that reads back as a default at run time.
//
// EXPECTED: compilation fails at the `static_assert` in `ToolDefaultKey::of`, naming ToolDefaults.def.

#include "core/settings/CommentingTypes.h"

using namespace rawpage::core;

QString probe()
{
    return ToolDefaultKey::of<CommentingTool::Pencil, ToolProperty::FontSize>().path();
}
