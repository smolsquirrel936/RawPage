// NEGATIVE CASE 2 for guide case A-41 -- naming a key by string.
//
// The spelling that matters more than the typo. A `get(QString)` overload is how every settings
// store in history acquires its second, undeclared half: it always arrives well-intentioned, for
// one key, in a hurry. There is no such overload, so this does not compile -- and I-15's lesson is
// that a rule tested against one spelling is a rule that catches one spelling.
//
// EXPECTED: compilation fails -- no matching function for `get`.

#include "core/settings/Settings.h"

using namespace rawpage::core;

QString probe(const Settings& settings)
{
    return settings.get(QStringLiteral("general/theme"));
}
