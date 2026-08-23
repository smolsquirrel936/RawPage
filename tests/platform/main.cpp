// The platform suite needs a QCoreApplication, which is why it has its own main rather than
// Catch2's.
//
// `WinSingleInstanceBus` delivers its handler by QUEUED INVOCATION onto the thread that called
// listen() -- that is the class's whole thread-affinity contract, and the reason a caller with a
// window never has to marshal a handoff itself. A queued invocation with no event loop and no
// QCoreApplication is a call that is posted and never runs, so a suite built on Catch2's own main
// would hang on every handoff case and look like a deadlock in the pipe code.

#include <catch2/catch_session.hpp>

#include <QCoreApplication>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
