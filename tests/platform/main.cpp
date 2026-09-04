// The platform suite needs a QCoreApplication, which is why it has its own main rather than
// Catch2's.
//
// `WinSingleInstanceBus` delivers its handler by QUEUED INVOCATION onto the thread that called
// listen() -- that is the class's whole thread-affinity contract, and the reason a caller with a
// window never has to marshal a handoff itself. A queued invocation with no event loop and no
// QCoreApplication is a call that is posted and never runs, so a suite built on Catch2's own main
// would hang on every handoff case and look like a deadlock in the pipe code.
//
// ---------------------------------------------------------------------------------------------
// PER-MONITOR-V2 AWARENESS, SET HERE AND BEFORE ANYTHING ELSE (Stage 2 T12).
//
// A process that has not asked for it is DPI-UNAWARE, and Windows hands a DPI-unaware caller
// VIRTUALIZED figures from every display query: the P-1 rig's 3840x2160 panel at 150 % reports
// itself as 2560x1440 at 100 %. Those numbers are self-consistent, so a suite that measured them
// would pass while asserting the opposite of the truth -- which is exactly what guide case A-60
// warns about when it says a rig measured only by the thing that might be broken is not a rig.
//
// `app/main.cpp` makes the same call for the product, before any Qt object exists. This is the
// suite's copy of it, and `WinDisplayTopology::processIsPerMonitorAware()` is asserted in the case
// itself so that a failure says which of the two went wrong.

#include <catch2/catch_session.hpp>

#include <QCoreApplication>

#include <windows.h>

int main(int argc, char** argv)
{
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
