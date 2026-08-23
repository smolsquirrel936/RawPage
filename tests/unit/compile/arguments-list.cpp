// THE CONTROL for argv-overload.cpp. Without it, "handing the parser argv does not compile" is
// equally satisfied by a header nobody can include at all -- which is exactly the failure T5's
// declared-key.cpp exists to rule out for the settings schema.
//
// EXPECTED: compiles.

#include "app/CommandLine.h"

#include <QStringList>

using namespace rawpage;
using namespace rawpage::app;

core::Result<Intent> probe(const QStringList& arguments)
{
    return CommandLine::parse(arguments);
}
