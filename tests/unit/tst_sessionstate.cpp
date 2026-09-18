// `core/settings/SessionState` -- capture, the file, and restore. Stage 4 T3; guide cases A-18,
// A-20, A-22, A-23 and A-24 (`Stage4-ViewingSurface-Testing-Guide.md` §5.5).
//
// HEADLESS BY CONSTRUCTION: this suite links no display and no PDF engine (`rawpage_tests`), which
// is what makes session restore -- the one Stage 4 feature that fails BETWEEN runs -- provable at
// all. Every file below is written into Catch2's own temporary directory, never into the session of
// whoever runs the suite: the trap Stage 3 T11 met with the recovery root, which the plan's §5.6
// restates for this class.
//
// The fingerprint and the clamp have their own file, `tst_fingerprint.cpp`.

#include "core/settings/SessionState.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <vector>

using rawpage::core::Guide;
using rawpage::core::Session;
using rawpage::core::SessionDocument;
using rawpage::core::SessionState;
using rawpage::core::SessionWindow;
using rawpage::core::render::FitMode;
using rawpage::core::render::LayoutMode;

namespace {

SessionDocument documentAt(const QString& path)
{
    SessionDocument document;
    document.path = path;
    document.id.path = path;
    document.id.size = 4096;
    document.id.modifiedMsecs = 1758240000000LL;
    document.id.contentHash = QByteArrayLiteral("\x01\x02\x03\x04");
    return document;
}

Session sessionOfThree(const QString& directory)
{
    Session session;
    session.fingerprint = QStringLiteral("deskarrangement");

    SessionWindow window;
    window.geometry = QRect(120, 90, 1400, 900);
    window.maximized = false;
    window.paneLayout = QStringLiteral("thumbnails:220");
    window.activeDocument = 1;

    SessionDocument first = documentAt(QDir(directory).filePath(QStringLiteral("one.pdf")));
    first.layoutMode = LayoutMode::TwoUpContinuous;
    first.fit = FitMode::Width;
    first.zoom = 1.37;
    first.pageIndex = 12;
    first.scrollX = 40.5;
    first.scrollY = 980.25;
    first.guides.push_back(Guide{Guide::Orientation::Horizontal, 72.0, 3, false});
    first.guides.push_back(Guide{Guide::Orientation::Vertical, 283.46, 3, true});

    SessionDocument second = documentAt(QDir(directory).filePath(QStringLiteral("two.pdf")));
    second.layoutMode = LayoutMode::Single;
    second.fit = FitMode::None;
    second.zoom = 2.0;
    second.pageIndex = 0;

    SessionDocument third = documentAt(QDir(directory).filePath(QStringLiteral("three.pdf")));
    third.fit = FitMode::Page;

    window.documents = {first, second, third};
    session.windows.push_back(window);
    return session;
}

// Every path exists. Restore's file check is a predicate precisely so a case can say so.
const SessionState::FileExists kEverythingExists = [](const QString&) { return true; };

} // namespace

TEST_CASE("a session round-trips through the file, field by field", "[session]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString file = QDir(scratch.path()).filePath(QStringLiteral("session.json"));

    const Session captured = sessionOfThree(scratch.path());
    {
        SessionState state(file);
        state.capture(captured);
        REQUIRE(state.save());
    }

    SessionState reloaded(file);
    const SessionState::LoadReport report = reloaded.load();
    REQUIRE(report.fileExisted);
    REQUIRE(report.loaded);
    REQUIRE(report.reason.isEmpty());

    const Session& session = reloaded.session();
    REQUIRE(session.fingerprint == captured.fingerprint);
    REQUIRE(session.windows.size() == 1);

    const SessionWindow& window = session.windows.front();
    REQUIRE(window.geometry == QRect(120, 90, 1400, 900));
    REQUIRE(window.maximized == false);
    REQUIRE(window.activeDocument == 1);
    REQUIRE(window.paneLayout == QStringLiteral("thumbnails:220"));
    REQUIRE(window.documents.size() == 3);

    // THE TAB ORDER IS THE LIST ORDER, and asserting it by name is the point: a set that came back
    // with the same three documents in another order would pass a count and fail a user.
    REQUIRE(window.documents[0].path.endsWith(QStringLiteral("one.pdf")));
    REQUIRE(window.documents[1].path.endsWith(QStringLiteral("two.pdf")));
    REQUIRE(window.documents[2].path.endsWith(QStringLiteral("three.pdf")));

    const SessionDocument& first = window.documents[0];
    REQUIRE(first.layoutMode == LayoutMode::TwoUpContinuous);
    REQUIRE(first.fit == FitMode::Width);
    REQUIRE(first.zoom == 1.37);
    REQUIRE(first.pageIndex == 12);
    REQUIRE(first.scrollX == 40.5);
    REQUIRE(first.scrollY == 980.25);
    REQUIRE(first.id.size == 4096);
    REQUIRE(first.id.modifiedMsecs == 1758240000000LL);
    REQUIRE(first.id.contentHash == QByteArrayLiteral("\x01\x02\x03\x04"));

    // The guides, which are the fields most likely to be dropped silently: they are the one part of
    // this file that has no other home (D4-7).
    REQUIRE(first.guides.size() == 2);
    REQUIRE(first.guides[0] == Guide{Guide::Orientation::Horizontal, 72.0, 3, false});
    REQUIRE(first.guides[1] == Guide{Guide::Orientation::Vertical, 283.46, 3, true});
    REQUIRE(window.documents[1].guides.empty());

    REQUIRE(window.documents[1].fit == FitMode::None);
    REQUIRE(window.documents[1].zoom == 2.0);
    REQUIRE(window.documents[2].fit == FitMode::Page);
}

TEST_CASE("an unknown arrangement falls back to the most recent one", "[session]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    SessionState state(QDir(scratch.path()).filePath(QStringLiteral("session.json")));

    Session desk = sessionOfThree(scratch.path());
    desk.fingerprint = QStringLiteral("desk");
    desk.windows.front().geometry = QRect(200, 150, 1600, 1000);
    state.capture(desk);

    const std::vector<QRect> laptopScreen{QRect(0, 0, 1920, 1080)};
    const SessionState::Restored restored =
        state.restore(QStringLiteral("a-projector-nobody-has-seen"), laptopScreen, kEverythingExists);

    REQUIRE(restored.usedFallbackArrangement);
    REQUIRE(restored.windows.size() == 1);
    // It came from `desk` rather than from nothing -- and then the clamp had its say, which is
    // A-21's half of this and is asserted there. Here the claim is only that the geometry is the
    // remembered one where the desktop allows it.
    REQUIRE(restored.windows.front().geometry == QRect(200, 150, 1600, 1000));
}

TEST_CASE("a known arrangement is used rather than the most recent", "[session]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    SessionState state(QDir(scratch.path()).filePath(QStringLiteral("session.json")));

    Session docked = sessionOfThree(scratch.path());
    docked.fingerprint = QStringLiteral("docked");
    docked.windows.front().geometry = QRect(300, 200, 1500, 900);
    state.capture(docked);

    Session laptop = sessionOfThree(scratch.path());
    laptop.fingerprint = QStringLiteral("laptop");
    laptop.windows.front().geometry = QRect(10, 20, 1200, 800);
    state.capture(laptop);   // now the most recent

    const std::vector<QRect> big{QRect(0, 0, 3840, 2160)};
    const SessionState::Restored restored =
        state.restore(QStringLiteral("docked"), big, kEverythingExists);

    // THE CONTROL for the fallback case above: when the arrangement IS known, the most recent one
    // must not win.
    REQUIRE_FALSE(restored.usedFallbackArrangement);
    REQUIRE(restored.windows.front().geometry == QRect(300, 200, 1500, 900));
}

TEST_CASE("every restored window is clamped onto a desktop that exists", "[session]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    SessionState state(QDir(scratch.path()).filePath(QStringLiteral("session.json")));

    // Captured across a 5760x2160 desktop: one window on the far-right monitor.
    Session wide = sessionOfThree(scratch.path());
    wide.fingerprint = QStringLiteral("three-monitors");
    wide.windows.front().geometry = QRect(4200, 1500, 1400, 600);
    SessionWindow second;
    second.geometry = QRect(2000, 300, 1200, 800);
    second.documents.push_back(documentAt(QDir(scratch.path()).filePath(QStringLiteral("x.pdf"))));
    wide.windows.push_back(second);
    state.capture(wide);

    // Restored onto a desktop one third the size.
    const std::vector<QRect> small{QRect(0, 0, 1920, 1080)};
    const SessionState::Restored restored =
        state.restore(QStringLiteral("three-monitors"), small, kEverythingExists);

    REQUIRE(restored.clamped);
    REQUIRE(restored.windows.size() == 2);
    for (const SessionWindow& window : restored.windows) {
        REQUIRE_FALSE(window.geometry.isEmpty());
        // Reachable: the whole window is on the one screen there is.
        REQUIRE(small.front().contains(window.geometry));
    }
}

TEST_CASE("a window that already fits is not moved when others are clamped", "[session]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    SessionState state(QDir(scratch.path()).filePath(QStringLiteral("session.json")));

    Session mixed = sessionOfThree(scratch.path());
    mixed.fingerprint = QStringLiteral("mixed");
    mixed.windows.front().geometry = QRect(64, 48, 1200, 800);   // already on screen
    SessionWindow stray;
    stray.geometry = QRect(5000, 1800, 900, 600);                // not
    mixed.windows.push_back(stray);
    state.capture(mixed);

    const std::vector<QRect> small{QRect(0, 0, 1920, 1080)};
    const SessionState::Restored restored =
        state.restore(QStringLiteral("mixed"), small, kEverythingExists);

    // The control, at the level of a whole restore rather than of one rectangle: clamping is per
    // window, so a session with one bad window does not shuffle the good ones.
    REQUIRE(restored.windows[0].geometry == QRect(64, 48, 1200, 800));
    REQUIRE(restored.windows[1].geometry != QRect(5000, 1800, 900, 600));
    REQUIRE(restored.clamped);
}

TEST_CASE("the arrangement store is bounded and evicts the least recently seen", "[session]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    SessionState state(QDir(scratch.path()).filePath(QStringLiteral("session.json")));

    // The bound is read from the code, not from the guide -- so raising it does not turn this case
    // red for the wrong reason.
    const std::size_t bound = SessionState::kMaxArrangements;
    for (std::size_t at = 0; at < bound + 4; ++at) {
        Session session = sessionOfThree(scratch.path());
        session.fingerprint = QStringLiteral("arrangement-%1").arg(at);
        state.capture(session);
    }

    REQUIRE(state.arrangements().size() == bound);
    // Most-recent-first, so the newest is at the front and the first four are gone.
    REQUIRE(state.arrangements().front().fingerprint
            == QStringLiteral("arrangement-%1").arg(bound + 3));
    for (const auto& arrangement : state.arrangements())
        REQUIRE(arrangement.fingerprint != QStringLiteral("arrangement-0"));
}

TEST_CASE("seeing an arrangement again moves it rather than duplicating it", "[session]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    SessionState state(QDir(scratch.path()).filePath(QStringLiteral("session.json")));

    Session desk = sessionOfThree(scratch.path());
    desk.fingerprint = QStringLiteral("desk");
    state.capture(desk);

    Session train = sessionOfThree(scratch.path());
    train.fingerprint = QStringLiteral("train");
    state.capture(train);

    desk.windows.front().geometry = QRect(400, 300, 1000, 700);
    state.capture(desk);   // back at the desk

    REQUIRE(state.arrangements().size() == 2);
    REQUIRE(state.arrangements().front().fingerprint == QStringLiteral("desk"));
    REQUIRE(state.arrangements().front().windows.front().rect == QRect(400, 300, 1000, 700));
}

TEST_CASE("a document that no longer exists is dropped and named once", "[session]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    SessionState state(QDir(scratch.path()).filePath(QStringLiteral("session.json")));

    Session session = sessionOfThree(scratch.path());
    state.capture(session);

    const QString missing = QDir(scratch.path()).filePath(QStringLiteral("two.pdf"));
    const SessionState::Restored restored =
        state.restore(session.fingerprint, {QRect(0, 0, 1920, 1080)},
                      [&](const QString& path) { return path != missing; });

    REQUIRE(restored.droppedPaths.size() == 1);
    REQUIRE(restored.droppedPaths.front() == missing);
    REQUIRE(restored.windows.front().documents.size() == 2);
    REQUIRE(restored.windows.front().documents[0].path.endsWith(QStringLiteral("one.pdf")));
    REQUIRE(restored.windows.front().documents[1].path.endsWith(QStringLiteral("three.pdf")));

    // The active tab was index 1, which was the document that went away. An index into a list that
    // just got shorter is answered here rather than left to whoever reads it.
    REQUIRE(restored.windows.front().activeDocument >= 0);
    REQUIRE(restored.windows.front().activeDocument < 2);

    // "and the next capture does not carry it": capturing what restore produced drops it for good.
    Session next;
    next.fingerprint = session.fingerprint;
    next.windows = restored.windows;
    state.capture(next);
    for (const SessionDocument& document : state.session().windows.front().documents)
        REQUIRE(document.path != missing);
}

TEST_CASE("nothing is dropped when every document is still there", "[session]")
{
    // The control for the case above: a restore that dropped a document nobody deleted would be
    // indistinguishable from a working one without this.
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    SessionState state(QDir(scratch.path()).filePath(QStringLiteral("session.json")));

    const Session session = sessionOfThree(scratch.path());
    state.capture(session);

    const SessionState::Restored restored =
        state.restore(session.fingerprint, {QRect(0, 0, 1920, 1080)}, kEverythingExists);
    REQUIRE(restored.droppedPaths.empty());
    REQUIRE(restored.windows.front().documents.size() == 3);
    REQUIRE(restored.windows.front().activeDocument == 1);
}

TEST_CASE("a session file we cannot use leaves the product startable", "[session]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    const auto loadOf = [](const QString& path) {
        SessionState state(path);
        return state.load();
    };
    const auto writeFile = [](const QString& path, const QByteArray& bytes) {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(bytes);
        file.close();
    };

    SECTION("no file at all is the first launch, not a fault")
    {
        const SessionState::LoadReport report =
            loadOf(QDir(scratch.path()).filePath(QStringLiteral("absent.json")));
        REQUIRE_FALSE(report.fileExisted);
        REQUIRE_FALSE(report.loaded);
        REQUIRE(report.reason.isEmpty());   // nothing to report: there was nothing there
    }

    SECTION("truncated JSON")
    {
        const QString path = QDir(scratch.path()).filePath(QStringLiteral("truncated.json"));
        writeFile(path, QByteArrayLiteral("{\"format\":1,\"session\":{\"windows\":[{\"x\":1"));
        const SessionState::LoadReport report = loadOf(path);
        REQUIRE(report.fileExisted);
        REQUIRE_FALSE(report.loaded);
        REQUIRE_FALSE(report.reason.isEmpty());
    }

    SECTION("an empty file")
    {
        const QString path = QDir(scratch.path()).filePath(QStringLiteral("empty.json"));
        writeFile(path, QByteArray{});
        const SessionState::LoadReport report = loadOf(path);
        REQUIRE(report.fileExisted);
        REQUIRE_FALSE(report.loaded);
        REQUIRE_FALSE(report.reason.isEmpty());
    }

    SECTION("a format from a newer build is ignored rather than guessed at")
    {
        const QString path = QDir(scratch.path()).filePath(QStringLiteral("future.json"));
        writeFile(path, QByteArrayLiteral("{\"format\":99,\"session\":{\"windows\":[]}}"));
        const SessionState::LoadReport report = loadOf(path);
        REQUIRE(report.fileExisted);
        REQUIRE_FALSE(report.loaded);
        REQUIRE(report.reason.contains(QStringLiteral("99")));
    }

    SECTION("a directory where the file should be")
    {
        const QString path = QDir(scratch.path()).filePath(QStringLiteral("session.json"));
        REQUIRE(QDir().mkpath(path));
        const SessionState::LoadReport report = loadOf(path);
        REQUIRE(report.fileExisted);
        REQUIRE_FALSE(report.loaded);
        REQUIRE_FALSE(report.reason.isEmpty());
    }
}

TEST_CASE("an unusable session file is replaced by the next clean quit", "[session]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString path = QDir(scratch.path()).filePath(QStringLiteral("session.json"));
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(QByteArrayLiteral("not json at all"));
    }

    SessionState state(path);
    REQUIRE_FALSE(state.load().loaded);
    state.capture(sessionOfThree(scratch.path()));
    REQUIRE(state.save());

    SessionState reloaded(path);
    REQUIRE(reloaded.load().loaded);
    REQUIRE(reloaded.session().windows.size() == 1);
}

TEST_CASE("restoring from a state that was never loaded gives nothing, not a crash", "[session]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    SessionState state(QDir(scratch.path()).filePath(QStringLiteral("nothing.json")));
    state.load();

    const SessionState::Restored restored =
        state.restore(QStringLiteral("whatever"), {QRect(0, 0, 1920, 1080)}, kEverythingExists);
    REQUIRE(restored.windows.empty());
    REQUIRE(restored.droppedPaths.empty());
    REQUIRE_FALSE(restored.usedFallbackArrangement);
    REQUIRE_FALSE(restored.clamped);
}
