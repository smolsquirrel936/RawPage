// NEGATIVE CASE 3 for guide case A-41 -- the right key, the wrong type.
//
// `Settings::get` returning `T` is only half of D1-10. The other half is that a caller cannot
// SET a key to something the key is not: `general/theme` holds a Theme, and assigning it an int
// would write a value the codec never produced and the reader would reject at the next launch --
// a setting that silently reverts, which is the hardest kind of bug to report.
//
// EXPECTED: compilation fails -- no matching function for `set`.

#include "core/settings/Settings.h"

using namespace rawpage::core;

void write(Settings& settings)
{
    settings.set(keys::general::theme, 3);
}
