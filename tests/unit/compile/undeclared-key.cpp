// NEGATIVE CASE 1 for guide case A-41 -- a misspelled key name.
//
// This is the everyday shape of the mistake: a key that looks right, in the right namespace, and
// does not exist. D1-10's guarantee is that this is a NAME THAT DOES NOT EXIST rather than a string
// that reads back as empty at runtime, so the compiler is the thing that reports it.
//
// EXPECTED: compilation fails, naming `uiFontScal` at this line.

#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"

using namespace rawpage::core;

double probe(const Settings& settings)
{
    return settings.get(keys::general::uiFontScal);
}
