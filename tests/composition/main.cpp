// The composition suite needs a QApplication on the offscreen platform, and the source catalog installed --
// the same two decisions `tests/ui/main.cpp` makes and explains, for the same reasons: a developer running the
// binary by hand gets CI's conditions, and the widgets are exercised against real text rather than their keys.

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

    QTranslator source;
    const QString catalog =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("i18n/rawpage_en.qm"));
    if (source.load(catalog)) QCoreApplication::installTranslator(&source);

    return Catch::Session().run(argc, argv);
}
