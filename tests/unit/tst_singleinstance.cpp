// Stage 1 T15 -- guide case A-61: `general/singleInstance=false` gives two windows.
//
// WHAT THIS FILE CAN PROVE, AND WHAT IT DELIBERATELY LEAVES TO A SCRIPT.
//
// A-61's wording is about two PROCESSES with two WINDOWS, and no test binary can be two processes.
// But the case has a mechanical core, and it is not the window count: whether a second launch
// forwards is decided entirely by `SingleInstance::claim(enabled)`, before any window exists. With
// `enabled` false the bus must never be TOUCHED -- not claimed, not asked whether it is primary --
// because a claim that happens and is then ignored still creates the `Local\` mutex, and the next
// launch of a build with the setting back on would find an owner nobody is listening behind.
//
// So this file asserts the policy against a bus that records every call, which is the half that can
// run in CI on every push. The two-process half is scripts/check-single-instance.ps1, which needs
// the real settings.ini and therefore cannot be a ctest case -- measured at T15: Qt resolves
// AppDataLocation through SHGetKnownFolderPath, so overriding the APPDATA environment variable for
// a child process does NOT redirect it, and a ctest case would rewrite the settings of whoever ran
// the suite.
//
// WHY IT IS IN THE HEADLESS SUITE. app/SingleInstance.cpp names core/ and platform/platform.h, and
// platform.h is an interface header that includes no OS header (its own layer-lint entry says so).
// It is compiled into rawpage_tests the way app/CommandLine.cpp and app/LogFile.cpp already are, so
// rawpage_assert_headless() still resolves to rawpage_core + rawpage_pdf -- guide case A-83 is
// untouched, and the day this file needs rawpage_platform to compile, configure fails and the
// finding is about SingleInstance rather than about the test.

#include <catch2/catch_test_macros.hpp>

#include "app/SingleInstance.h"

#include <QStringList>

using namespace rawpage;
using rawpage::app::SingleInstance;

namespace {

// Records rather than simulates. The point of A-61 is what is NOT called.
class RecordingBus final : public platform::ISingleInstanceBus {
public:
    int         claims   = 0;
    int         listens  = 0;
    int         forwards = 0;
    int         releases = 0;
    QString     lastScope;
    QStringList lastFiles;

    // What `claim()` will answer. False makes this process a secondary.
    bool claimSucceeds = true;

    core::Result<bool> claim(const QString& scopeId) override
    {
        ++claims;
        lastScope = scopeId;
        primary_  = claimSucceeds;
        return claimSucceeds;
    }

    bool isPrimary() const override { return primary_; }

    core::Status listen(OpenFilesHandler) override
    {
        ++listens;
        return core::Status{};
    }

    core::Status forward(const QStringList& files) override
    {
        ++forwards;
        lastFiles = files;
        return core::Status{};
    }

    void release() override { ++releases; }

private:
    bool primary_ = false;
};

} // namespace

TEST_CASE("A-61: singleInstance=false never touches the bus", "[unit][singleinstance]")
{
    RecordingBus   bus;
    SingleInstance instance(&bus, QStringLiteral("RawPage-test"));

    const core::Result<SingleInstance::Role> role = instance.claim(/*enabled=*/false);

    REQUIRE(role);
    CHECK(role.value() == SingleInstance::Role::Disabled);

    // THE ASSERTION THAT IS THE CASE. Not "we decided not to forward" -- no OS object was created
    // at all, so there is nothing for a second launch to find and nothing left behind. An
    // implementation that claimed and then discarded the answer would pass a test that only looked
    // at the returned Role.
    CHECK(bus.claims == 0);
    CHECK(bus.listens == 0);
    CHECK(bus.forwards == 0);
}

TEST_CASE("A-61: a disabled instance refuses to forward or listen", "[unit][singleinstance]")
{
    RecordingBus   bus;
    SingleInstance instance(&bus, QStringLiteral("RawPage-test"));
    REQUIRE(instance.claim(/*enabled=*/false));

    // Both directions, because Disabled is reached from two places -- the setting being off and the
    // platform bundle having no bus -- and a caller that got the role wrong must be refused rather
    // than silently doing nothing. `Unavailable` is the code the CLI and the batch runner can tell
    // apart from `NotFound`; see the T12 deviation about file.open.
    const core::Status forwarded = instance.forward({QStringLiteral("a.pdf")});
    REQUIRE_FALSE(forwarded);
    CHECK(forwarded.error().code == core::ErrorCode::Unavailable);

    const core::Status listening = instance.listen([](const QStringList&) {});
    REQUIRE_FALSE(listening);
    CHECK(listening.error().code == core::ErrorCode::Unavailable);

    CHECK(bus.forwards == 0);
    CHECK(bus.listens == 0);
}

// THE CONTROL. Without it every assertion above is satisfied by a claim() that does nothing for any
// value of `enabled` -- which is I-15's single-spelling finding wearing a boolean costume.
TEST_CASE("A-61 control: singleInstance=true does claim", "[unit][singleinstance]")
{
    SECTION("first launch becomes the primary and listens")
    {
        RecordingBus bus;
        bus.claimSucceeds = true;
        SingleInstance instance(&bus, QStringLiteral("RawPage-test"));

        const core::Result<SingleInstance::Role> role = instance.claim(/*enabled=*/true);
        REQUIRE(role);
        CHECK(role.value() == SingleInstance::Role::Primary);
        CHECK(bus.claims == 1);
        CHECK(bus.lastScope == QStringLiteral("RawPage-test"));

        REQUIRE(instance.listen([](const QStringList&) {}));
        CHECK(bus.listens == 1);
    }

    SECTION("second launch becomes a secondary and forwards its files")
    {
        RecordingBus bus;
        bus.claimSucceeds = false;
        SingleInstance instance(&bus, QStringLiteral("RawPage-test"));

        const core::Result<SingleInstance::Role> role = instance.claim(/*enabled=*/true);
        REQUIRE(role);
        CHECK(role.value() == SingleInstance::Role::Secondary);

        // The path is non-ASCII on purpose: I-04's rule is that nothing may lose bytes, and this is
        // the last hop before the named pipe that A-67 already round-trips.
        REQUIRE(instance.forward({QString::fromUtf8("\xe6\xb8\xac\xe8\xa9\xa6.pdf")}));
        CHECK(bus.forwards == 1);
        REQUIRE(bus.lastFiles.size() == 1);
        CHECK(bus.lastFiles.at(0) == QString::fromUtf8("\xe6\xb8\xac\xe8\xa9\xa6.pdf"));
    }
}

// A null bus is a legal state of platform::Platform (convention 6: an unimplemented interface is a
// null member, never a stub), and the honest response is Disabled rather than a crash. This is the
// second road to Disabled and it is the one nobody exercises by hand.
TEST_CASE("A-61: no bus at all is Disabled, not a crash", "[unit][singleinstance]")
{
    SingleInstance instance(nullptr, QStringLiteral("RawPage-test"));

    const core::Result<SingleInstance::Role> role = instance.claim(/*enabled=*/true);
    REQUIRE(role);
    CHECK(role.value() == SingleInstance::Role::Disabled);
    CHECK_FALSE(instance.forward({QStringLiteral("a.pdf")}));
}

// D1-11's scope rule, asserted where it is decided rather than where it is used. Two installs are
// two applications; one install reached two ways is one application.
TEST_CASE("A-61: the scope id is per install directory, case-folded", "[unit][singleinstance]")
{
    const QString a = SingleInstance::scopeIdForExecutable(
        QStringLiteral("C:/Program Files/RawPage/rawpage.exe"));
    const QString b = SingleInstance::scopeIdForExecutable(
        QStringLiteral("c:/program files/rawpage/rawpage.exe"));
    const QString sameDirOtherName = SingleInstance::scopeIdForExecutable(
        QStringLiteral("C:/Program Files/RawPage/rawpage-debug.exe"));
    const QString elsewhere = SingleInstance::scopeIdForExecutable(
        QStringLiteral("D:/build/x64-debug/bin/rawpage.exe"));

    CHECK(a == b);
    CHECK(a == sameDirOtherName);
    CHECK(a != elsewhere);
    CHECK(a.startsWith(QStringLiteral("RawPage-")));
}
