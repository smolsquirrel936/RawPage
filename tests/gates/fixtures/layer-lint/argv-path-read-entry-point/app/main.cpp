// FIXTURE, NEGATIVE CONTROL -- app/main.cpp is the one file that legitimately holds argv. It hands
// it to QApplication and prints it for the --print-args measurement that established the rule, and
// a gate that forbade argv everywhere would make that measurement unwritable.
#include <QString>

namespace {

void printArgv(int argc, char** argv)
{
    for (int i = 0; i < argc; ++i) {
        const QString mangled = QString::fromLocal8Bit(argv[i]);
        (void)mangled;
    }
}

} // namespace

int main(int argc, char** argv)
{
    printArgv(argc, argv);
    return 0;
}
