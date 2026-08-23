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

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QTranslator>

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

    return Catch::Session().run(argc, argv);
}
