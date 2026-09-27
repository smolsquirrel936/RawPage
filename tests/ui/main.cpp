// The widget suite needs a QApplication -- and it needs one on the offscreen platform.
//
// WHY THE PLATFORM IS SET HERE RATHER THAN IN THE CTEST ENVIRONMENT ALONE. It is set in both, and
// the redundancy is deliberate: a developer running the binary by hand gets the same conditions CI
// does. A widget test that quietly opens real windows on somebody's desktop is a test that behaves
// differently on the one machine where it matters, and the difference shows up as a flake nobody
// can reproduce.
//
// D1-4: this target links rawpage_core and rawpage_ui and NOTHING ELSE. It never runs on
// portable-asan -- the portable subset is core/ + pdf/cos/ by rule.

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFontDatabase>
#include <QTranslator>
#include <QWidget>

#include <cstdio>
#include <string>
#include <vector>

namespace {

// =============================================================================================
// THE PROCESS-STATE GUARD -- Stage 4 guide I-28, case A-103.
//
// Every case in this binary shares ONE QApplication, and `ctest` hides that: it runs each case in a
// process of its own, so a case that leaves something behind is only ever followed by nothing. Run
// whole, the binary died with an access violation, `std::bad_alloc` or no output at all -- a
// different case every time, because Catch2 v3 shuffles by default -- and every one of those cases
// passed alone. Two kinds of leftover did it, each measured:
//
//   * A WIDGET STILL ALIVE after its case: a closed tab is a `deleteLater()`, and one left pending
//     ran its destructor in a LATER case, against a fixture that no longer existed.
//   * THE APPLICATION'S FONT CHANGED: a case that applied a UI font scale of 2.0, and two that
//     loaded Arial into the font database, moved the layout of every case after them.
//
// So after every case this checks the three things that were seen to leak, and fails the PROCESS
// naming the case -- which under `ctest` is the leaking case itself, deterministically, rather than
// some later victim under some seed. It reports; it does not clean up, because a guard that tidied
// the state away would make the leak invisible again.
//
// WHAT IT DOES NOT CHECK (convention 19): the application PALETTE -- `MainWindow`'s theme writes it
// in nearly every shell case, and a palette moves no geometry -- nor any other process-wide Qt
// state (style, translators, focus, the clipboard). A case that leaks one of those is not caught.
class ProcessStateGuard : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    static std::vector<std::string>& violations()
    {
        static std::vector<std::string> found;
        return found;
    }

    void testCaseStarting(const Catch::TestCaseInfo&) override
    {
        fontPointSize_ = QApplication::font().pointSizeF();
        fontFamilies_ = QFontDatabase::families().size();
    }

    void testCaseEnded(const Catch::TestCaseStats& stats) override
    {
        std::string found;
        if (const qsizetype alive = QApplication::allWidgets().size(); alive != 0)
            found += " " + std::to_string(alive) + " widget(s) still alive;";
        if (const double now = QApplication::font().pointSizeF(); now != fontPointSize_)
            found += " the application font went from " + std::to_string(fontPointSize_) + " pt to "
                     + std::to_string(now) + " pt;";
        if (const qsizetype now = QFontDatabase::families().size(); now != fontFamilies_)
            found += " the font database went from " + std::to_string(fontFamilies_) + " to "
                     + std::to_string(now) + " families;";
        if (found.empty()) return;

        const std::string line = "PROCESS STATE LEAKED by \"" + stats.testInfo->name + "\":" + found;
        std::fprintf(stderr, "%s\n", line.c_str());
        std::fflush(stderr);
        violations().push_back(line);
    }

private:
    double    fontPointSize_ = 0.0;
    qsizetype fontFamilies_ = 0;
};

} // namespace

CATCH_REGISTER_LISTENER(ProcessStateGuard)

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication app(argc, argv);

    // THE SOURCE CATALOG IS INSTALLED, and that is a decision rather than convenience. The catalog
    // key IS the source string in this project, so without a translator every string in the shell
    // resolves to its own key -- which means these tests would exercise a UI made entirely of
    // `shell.status.ready`, and a format string like `shell.placeholder.interfaces` would have no
    // %1 for its argument to land in. Testing the shell against text nobody will ever see is a
    // suite that cannot notice a key whose catalog entry was never written.
    //
    // The .qm sits beside this binary because tests/ui depends on the rawpage_translations target,
    // exactly as rawpage.exe does. If it is missing the tests still run -- and the string cases
    // fail, naming the missing catalog, which is the correct outcome rather than a skip.
    QTranslator source;
    const QString catalog =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("i18n/rawpage_en.qm"));
    if (source.load(catalog)) QCoreApplication::installTranslator(&source);

    const int failed = Catch::Session().run(argc, argv);

    // Every case passed and one left the process dirty: the run still FAILS, and says which.
    const auto& leaked = ProcessStateGuard::violations();
    if (leaked.empty()) return failed;
    std::fprintf(stderr, "\n%zu case(s) left process-wide state behind (Stage 4 guide I-28):\n",
                 leaked.size());
    for (const std::string& line : leaked) std::fprintf(stderr, "  %s\n", line.c_str());
    return failed != 0 ? failed : 1;
}
