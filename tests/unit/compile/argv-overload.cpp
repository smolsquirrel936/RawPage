// NEGATIVE CASE for issue I-04 -- handing the command-line parser argc/argv.
//
// The runtime half of this rule cannot be tested at runtime: a mangled path is a perfectly valid
// QString, and a test that compared one against another that had been through the same transcode
// would pass while both were wrong. So the guarantee has to be structural, and it is: there is no
// `parse(int, char**)` overload to call, and there never will be.
//
// The other half of the rule -- that nothing CONSTRUCTS a QStringList out of argv and passes that
// in -- is held by tools/layer-lint's `argv-path-read` rule, which is not escapable. Two
// mechanisms, because either alone has a hole the other covers.
//
// EXPECTED: compilation fails -- no matching function for `parse`.

#include "app/CommandLine.h"

using namespace rawpage;
using namespace rawpage::app;

core::Result<Intent> probe(int argc, char** argv)
{
    return CommandLine::parse(argc, argv);
}
