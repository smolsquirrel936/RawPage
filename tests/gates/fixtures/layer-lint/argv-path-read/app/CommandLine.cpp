// FIXTURE -- a violation by construction. Issue I-04's rule, in the exact place it would be
// broken: a command-line parser reading the file path out of argv instead of out of
// QCoreApplication::arguments(). On a machine whose ANSI code page cannot represent the path,
// this compiles, runs, and opens the wrong file -- or none.
#include <QString>
#include <QStringList>

namespace rawpage::app {

QStringList filesFrom(int argc, char** argv)
{
    QStringList files;
    for (int i = 1; i < argc; ++i) {
        files << QString::fromLocal8Bit(argv[i]);
    }
    return files;
}

} // namespace rawpage::app
