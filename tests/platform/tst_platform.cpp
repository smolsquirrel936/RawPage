// Stage 1 T7 -- platform/platform.h, PlatformFactory, WinSingleInstanceBus, WinShellIntegration.
//
// Guide cases A-62 (the mutex is `Local\`) and the mechanical half of A-60. A-60 and A-61 proper are
// end-to-end through `rawpage.exe` and cannot run until T8 wires the command line; what CAN run now
// is the bus itself, and it is the component whose failure mode -- "I double-clicked and nothing
// happened" -- is the least visible in the product.
//
// WHY A THIRD TEST TARGET. `tests/unit/`'s `rawpage_assert_headless()` forbids linking
// `rawpage_platform`, deliberately: it is the standing proof that `core/` needs no OS, and it is
// what `portable-asan` verifies on Linux. D1-4 met the same pressure from `ui/` with a second
// target rather than by weakening the first. This is that decision applied again, to the same
// pressure, from the other side.
//
// THE SINGLE-PROCESS HANDOFF IS A REAL HANDOFF. `CreateMutexW` returns ERROR_ALREADY_EXISTS for a
// name the SAME process already holds, so a second bus object in this process is a genuine
// secondary: it takes the secondary branch, opens the real pipe, and the real server thread reads
// it. What that cannot cover is cross-process foreground activation, which has no offscreen
// equivalent and is manual case M-07.

#include <catch2/catch_test_macros.hpp>

#include "platform/platform.h"
#include "platform/win/PlatformFactory.h"
#include "platform/win/WinShellIntegration.h"
#include "platform/win/WinSingleInstanceBus.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <QUuid>

#include <set>

using namespace rawpage::platform;

namespace {

// A scope id unique to this run, so two `ctest -j` invocations, or a developer running the suite
// while the app is open, cannot claim each other's name. A shared name here would be a flake that
// only ever reproduces on a busy machine.
QString uniqueScope(const char* label)
{
    return QStringLiteral("test.%1.%2")
        .arg(QLatin1String(label))
        .arg(QUuid::createUuid().toString(QUuid::Id128));
}

// Pumps the calling thread's event loop until `predicate` holds or the budget runs out. The bus
// delivers its handler by queued invocation, so nothing arrives without this -- and a fixed sleep
// would be both slower and flakier.
bool pumpUntil(const std::function<bool()>& predicate, int budgetMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < budgetMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return predicate();
}

} // namespace

// =======================================================================================
// The boundary itself
// =======================================================================================

TEST_CASE("every declared interface has a name, and the names are unique", "[platform]")
{
    const auto& names = platformInterfaceNames();
    REQUIRE(names.size() == kPlatformInterfaceCount);

    std::set<std::string_view> seen;
    for (const std::string_view name : names) {
        INFO("interface " << std::string(name));
        CHECK(!name.empty());
        CHECK(name.front() == 'I');          // the boundary is interfaces; a concrete type here is a bug
        CHECK(seen.insert(name).second);
    }
}

TEST_CASE("an empty bundle reports every interface as unimplemented", "[platform]")
{
    // The census must be derived from the bundle rather than hard-coded, or "not implemented yet"
    // becomes a number somebody forgets to update and a diagnostic nobody can trust.
    const Platform empty;
    CHECK(unimplementedInterfaces(empty).size() == kPlatformInterfaceCount);
}

// FOUR AS OF STAGE 2 T12, and the number is in the name on purpose. This case is the census, and a
// census whose title says "two" while its body says four is the shape of drift the T1 recount was
// written to stop. Stage 1 wired `ISingleInstanceBus` and `IShellIntegration`; T12 adds
// `IDisplayTopology` and `IPowerSession` -- the two the render policy layer was waiting on.
TEST_CASE("the Windows bundle implements exactly the four interfaces built so far", "[platform]")
{
    win::PlatformOptions options;
    options.executablePath = QStringLiteral("C:/nowhere/rawpage.exe");
    options.registryRoot = QStringLiteral("Software\\RawPageTests\\NotUsed");

    const std::unique_ptr<Platform> platform = win::createWindowsPlatform(options);
    REQUIRE(platform != nullptr);

    CHECK(platform->singleInstanceBus != nullptr);
    CHECK(platform->shellIntegration != nullptr);
    CHECK(platform->displayTopology != nullptr);
    CHECK(platform->powerSession != nullptr);

    const std::vector<std::string_view> missing = unimplementedInterfaces(*platform);
    CHECK(missing.size() == kPlatformInterfaceCount - 4);

    // Named, not just counted: a bundle that implemented four DIFFERENT interfaces would satisfy a
    // count and would mean the factory wired the wrong things.
    for (const std::string_view name : missing) {
        CHECK(name != "ISingleInstanceBus");
        CHECK(name != "IShellIntegration");
        CHECK(name != "IDisplayTopology");
        CHECK(name != "IPowerSession");
    }
}

// =======================================================================================
// A-62 -- the OS object names
// =======================================================================================

TEST_CASE("the mutex is in the Local namespace, never Global", "[platform][singleinstance]")
{
    // D1-11. A `Global\` name means the second user on a machine cannot run the app at all -- a
    // support incident nobody diagnoses quickly, because it looks like the install is broken.
    const QString name = win::singleInstanceMutexName(QStringLiteral("shipping"));
    CHECK(name.startsWith(QStringLiteral("Local\\")));
    CHECK_FALSE(name.contains(QStringLiteral("Global\\")));
    CHECK(name.contains(QStringLiteral("RawPage")));
}

TEST_CASE("the pipe name carries the session id", "[platform][singleinstance]")
{
    // The half that looks symmetric with the mutex and is not: named pipes have no `Local\`
    // namespace at all, so without the session id two users' correctly-separated mutexes would
    // still meet on one machine-wide pipe.
    const QString a = win::singleInstancePipeName(QStringLiteral("shipping"), 1);
    const QString b = win::singleInstancePipeName(QStringLiteral("shipping"), 2);

    CHECK(a.startsWith(QStringLiteral("\\\\.\\pipe\\")));
    CHECK(a != b);
    CHECK(a.endsWith(QStringLiteral(".1")));
    CHECK(b.endsWith(QStringLiteral(".2")));
}

TEST_CASE("a scope id that would escape the namespace is sanitized", "[platform][singleinstance]")
{
    // A backslash is the namespace separator: left in, it would silently create the object
    // somewhere other than where the caller believes, and the two instances would never see each
    // other. A caller passing a path or a version string is entirely plausible.
    const QString name = win::singleInstanceMutexName(QStringLiteral("C:\\builds\\Global\\x"));
    CHECK(name.startsWith(QStringLiteral("Local\\RawPage.SingleInstance.")));
    CHECK(name.count(QLatin1Char('\\')) == 1);

    const QString empty = win::singleInstanceMutexName(QString());
    CHECK(empty.endsWith(QStringLiteral("default")));
}

TEST_CASE("two scopes do not collide", "[platform][singleinstance]")
{
    CHECK(win::singleInstanceMutexName(QStringLiteral("a"))
          != win::singleInstanceMutexName(QStringLiteral("b")));
}

// =======================================================================================
// The claim / forward / deliver round trip
// =======================================================================================

TEST_CASE("the first claim is primary and the second is not", "[platform][singleinstance]")
{
    const QString scope = uniqueScope("claim");

    win::WinSingleInstanceBus first;
    const Result<bool> firstClaim = first.claim(scope);
    REQUIRE(firstClaim);
    CHECK(firstClaim.value());
    CHECK(first.isPrimary());

    win::WinSingleInstanceBus second;
    const Result<bool> secondClaim = second.claim(scope);
    REQUIRE(secondClaim);
    CHECK_FALSE(secondClaim.value());
    CHECK_FALSE(second.isPrimary());
}

TEST_CASE("releasing the claim lets the next instance become primary", "[platform][singleinstance]")
{
    // Otherwise a crashed or closed primary leaves the name held for the life of the session, and
    // every subsequent launch forwards into a process that is gone.
    const QString scope = uniqueScope("release");

    {
        win::WinSingleInstanceBus first;
        REQUIRE(first.claim(scope));
        CHECK(first.isPrimary());
    }

    win::WinSingleInstanceBus second;
    const Result<bool> claim = second.claim(scope);
    REQUIRE(claim);
    CHECK(claim.value());
}

TEST_CASE("a secondary's file list reaches the primary's handler", "[platform][singleinstance]")
{
    const QString scope = uniqueScope("handoff");

    win::WinSingleInstanceBus primary;
    REQUIRE(primary.claim(scope));
    REQUIRE(primary.isPrimary());

    QStringList received;
    REQUIRE(primary.listen([&received](const QStringList& files) { received = files; }));

    win::WinSingleInstanceBus secondary;
    REQUIRE(secondary.claim(scope));
    REQUIRE_FALSE(secondary.isPrimary());

    const QStringList sent{QStringLiteral("C:/docs/a.pdf"), QStringLiteral("C:/docs/b.pdf")};
    REQUIRE(secondary.forward(sent));

    REQUIRE(pumpUntil([&received] { return !received.isEmpty(); }));
    CHECK(received == sent);
}

TEST_CASE("a non-ASCII path survives the handoff byte for byte", "[platform][singleinstance]")
{
    // I-04's rule at a second process boundary. The pipe carries UTF-8 and the ACP never touches
    // it -- which is the whole reason the message is framed and encoded explicitly rather than
    // handed to anything that might transcode.
    const QString scope = uniqueScope("unicode");

    win::WinSingleInstanceBus primary;
    REQUIRE(primary.claim(scope));
    QStringList received;
    REQUIRE(primary.listen([&received](const QStringList& files) { received = files; }));

    win::WinSingleInstanceBus secondary;
    REQUIRE(secondary.claim(scope));

    const QStringList sent{QString::fromUtf8("C:/\xE6\x96\x87\xE4\xBB\xB6/\xE5\xA0\xB1\xE5\x91\x8A.pdf"),
                           QString::fromUtf8("C:/docs/report-\xF0\x9F\x93\x84.pdf")};
    REQUIRE(secondary.forward(sent));

    REQUIRE(pumpUntil([&received] { return !received.isEmpty(); }));
    CHECK(received == sent);
}

TEST_CASE("several handoffs in a row all arrive", "[platform][singleinstance]")
{
    // One connection per handoff, so this is the case that would catch a server loop that accepts
    // exactly once -- which is what the obvious implementation does and what a single-shot test
    // would never notice.
    const QString scope = uniqueScope("repeat");

    win::WinSingleInstanceBus primary;
    REQUIRE(primary.claim(scope));

    QStringList all;
    REQUIRE(primary.listen([&all](const QStringList& files) { all += files; }));

    win::WinSingleInstanceBus secondary;
    REQUIRE(secondary.claim(scope));

    for (int i = 0; i < 3; ++i) {
        REQUIRE(secondary.forward(QStringList{QStringLiteral("C:/docs/%1.pdf").arg(i)}));
        REQUIRE(pumpUntil([&all, i] { return all.size() == i + 1; }));
    }

    REQUIRE(all.size() == 3);
    CHECK(all.at(2) == QStringLiteral("C:/docs/2.pdf"));
}

TEST_CASE("the primary refuses to forward, and a secondary refuses to listen",
          "[platform][singleinstance]")
{
    const QString scope = uniqueScope("roles");

    win::WinSingleInstanceBus primary;
    REQUIRE(primary.claim(scope));
    const Status forwarded = primary.forward(QStringList{QStringLiteral("C:/x.pdf")});
    CHECK_FALSE(forwarded);
    CHECK(forwarded.error().code == rawpage::core::ErrorCode::InvalidArgument);

    win::WinSingleInstanceBus secondary;
    REQUIRE(secondary.claim(scope));
    const Status listened = secondary.listen([](const QStringList&) {});
    CHECK_FALSE(listened);
    // Distinct from InvalidArgument on purpose: "you are not the primary" is a state, not a
    // malformed call, and a caller may legitimately try both in order.
    CHECK(listened.error().code == rawpage::core::ErrorCode::Unavailable);
}

TEST_CASE("forwarding before claiming is refused rather than silently dropped",
          "[platform][singleinstance]")
{
    win::WinSingleInstanceBus bus;
    const Status forwarded = bus.forward(QStringList{QStringLiteral("C:/x.pdf")});
    CHECK_FALSE(forwarded);
}

TEST_CASE("release is idempotent and safe on a bus that never claimed", "[platform][singleinstance]")
{
    win::WinSingleInstanceBus never;
    never.release();
    never.release();

    win::WinSingleInstanceBus claimed;
    REQUIRE(claimed.claim(uniqueScope("idempotent")));
    claimed.release();
    claimed.release();
    CHECK_FALSE(claimed.isPrimary());
}

// =======================================================================================
// Shell integration -- file association only at Stage 1
// =======================================================================================

TEST_CASE("the association can be queried without changing anything", "[platform][shell]")
{
    // Read-only by construction. The obvious caller is a Preferences page that opens on every
    // launch, and a query with a side effect there would rewrite a user's registry on every run.
    win::WinShellIntegration shell(QStringLiteral("C:/nowhere/rawpage.exe"),
                                   QStringLiteral("Software\\RawPageTests\\ReadOnly"));

    const Result<IShellIntegration::AssociationStatus> status = shell.pdfAssociation();
    REQUIRE(status);
    CHECK_FALSE(status.value().progIdRegistered);   // nothing was ever written under this root
}

TEST_CASE("registering and unregistering round-trips under a scratch root", "[platform][shell]")
{
    // The write half runs FOR REAL, against a scratch key rather than `Software\Classes`, so it
    // never touches the association a person is using on this machine. A test that skipped the
    // write would be testing the half that never breaks.
    const QString root = QStringLiteral("Software\\RawPageTests\\%1")
                             .arg(QUuid::createUuid().toString(QUuid::Id128));

    win::WinShellIntegration shell(QStringLiteral("C:/Program Files/RawPage/rawpage.exe"), root);

    {
        const Result<IShellIntegration::AssociationStatus> before = shell.pdfAssociation();
        REQUIRE(before);
        REQUIRE_FALSE(before.value().progIdRegistered);
    }

    REQUIRE(shell.registerPdfAssociation());

    {
        const Result<IShellIntegration::AssociationStatus> after = shell.pdfAssociation();
        REQUIRE(after);
        CHECK(after.value().progIdRegistered);
    }

    REQUIRE(shell.unregisterPdfAssociation());

    {
        const Result<IShellIntegration::AssociationStatus> gone = shell.pdfAssociation();
        REQUIRE(gone);
        CHECK_FALSE(gone.value().progIdRegistered);
    }

    // Unregistering twice is not an error: an uninstaller runs after a user may already have
    // cleaned up by hand, and failing there turns a tidy machine into a failed uninstall.
    CHECK(shell.unregisterPdfAssociation());
}

TEST_CASE("registration without an executable path is refused", "[platform][shell]")
{
    // Rather than writing a command line that opens nothing -- a registration that exists, appears
    // in Open With, and does nothing when chosen is worse than no registration at all.
    win::WinShellIntegration shell(QString(), QStringLiteral("Software\\RawPageTests\\NoExe"));
    const Status registered = shell.registerPdfAssociation();
    CHECK_FALSE(registered);
    CHECK(registered.error().code == rawpage::core::ErrorCode::InvalidArgument);
}

TEST_CASE("the ProgID is the permanent public name it claims to be", "[platform][shell]")
{
    // It is written into a user's registry and read back by later versions, exactly as an ActionId
    // string is. This case exists so that renaming it is a deliberate act with a visible cost.
    CHECK(QLatin1String(win::kPdfProgId) == QLatin1String("RawPage.Document.1"));

    win::WinShellIntegration shell(QStringLiteral("C:/x/rawpage.exe"),
                                   QStringLiteral("Software\\Classes"));
    CHECK(shell.progIdKeyPath() == QStringLiteral("Software\\Classes\\RawPage.Document.1"));
}
