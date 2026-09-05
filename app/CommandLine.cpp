#include "app/CommandLine.h"

#include <QLatin1String>

namespace rawpage::app {
namespace CommandLine {
namespace {

using core::Error;
using core::ErrorCode;
using core::Result;

// `--lang zh-TW` and `--lang=zh-TW` are both spelled by real people and by real shell scripts, so
// both are accepted. Splitting on the first '=' rather than on any '=' matters for Stage 14, where
// a value may legitimately contain one.
struct Option {
    QString name;    // including the leading dashes
    QString value;   // from the `=` form; empty when the value follows as a separate argument
    bool    hasInlineValue = false;
};

Option splitOption(const QString& argument)
{
    Option option;
    const qsizetype equals = argument.indexOf(QLatin1Char('='));
    if (equals < 0) {
        option.name = argument;
        return option;
    }
    option.name = argument.left(equals);
    option.value = argument.mid(equals + 1);
    option.hasInlineValue = true;
    return option;
}

} // namespace

const QStringList& diagnosticSwitches()
{
    // Kept in one place so app/main.cpp's handler and this parser cannot drift apart: a switch
    // main.cpp answers but the parser does not know about would be reported as an unknown option
    // by a parser that never got to run, which is a bug nobody would look for here.
    static const QStringList switches{
        QStringLiteral("--version"),
        QStringLiteral("--print-dpi-awareness"),
        QStringLiteral("--print-args"),
        QStringLiteral("--crash-test"),
    };
    return switches;
}

Result<Intent> parse(const QStringList& arguments)
{
    Intent intent;

    // The diagnostics win outright, before anything else is looked at. --print-args in particular
    // is handed a list of paths that are its SUBJECT rather than files to open, and --version is
    // routinely typed with other arguments still on the line from a previous command.
    for (const QString& argument : arguments) {
        if (diagnosticSwitches().contains(argument)) {
            intent.kind = Intent::Kind::Diagnostic;
            intent.diagnostic = argument;
            return intent;
        }
    }

    // Index 0 is the program name. An empty list is legal -- see the header.
    bool literalsOnly = false;
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        const QString& argument = arguments.at(i);

        if (literalsOnly) {
            intent.files << argument;
            continue;
        }

        // `-` alone is a path (or stdin, at Stage 14); it is not an option.
        if (argument.size() < 2 || !argument.startsWith(QLatin1Char('-'))) {
            intent.files << argument;
            continue;
        }

        if (argument == QLatin1String("--")) {
            literalsOnly = true;
            continue;
        }

        const Option option = splitOption(argument);

        if (option.name == QLatin1String("--help") || option.name == QLatin1String("-h")
            || option.name == QLatin1String("-?")) {
            intent.kind = Intent::Kind::ShowHelp;
            return intent;
        }

        if (option.name == QLatin1String("--dump-ui-strings")) {
            intent.dumpUiStrings = true;
            continue;
        }

        if (option.name == QLatin1String("--list-actions")) {
            intent.listActions = true;
            continue;
        }

        if (option.name == QLatin1String("--lang")) {
            if (option.hasInlineValue) {
                if (option.value.isEmpty()) {
                    return Error{ErrorCode::InvalidArgument, QLatin1String("error.cli.missingValue"),
                                 option.name};
                }
                intent.locale = option.value;
                continue;
            }
            if (i + 1 >= arguments.size()) {
                return Error{ErrorCode::InvalidArgument, QLatin1String("error.cli.missingValue"),
                             option.name};
            }
            intent.locale = arguments.at(++i);
            continue;
        }

        // Unknown. Reported rather than ignored: a mistyped switch that is silently dropped is a
        // batch run at Stage 14 that reports success and did something else.
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.cli.unknownOption"), option.name};
    }

    return intent;
}

QString helpText()
{
    // Untranslated on purpose -- see the header. The list is what Stage 1 understands, not what the
    // product will one day accept; promising a verb here that does not exist would be worse than
    // saying less.
    return QStringLiteral(
        "RawPage -- Windows PDF editor\n"
        "\n"
        "Usage: rawpage.exe [options] [file...]\n"
        "\n"
        "Options:\n"
        "  --lang <locale>         Use this UI locale for this run (for example zh-TW).\n"
        "  --dump-ui-strings       Print the shell's resolved catalog strings and exit.\n"
        "  --list-actions          Print every declared command with its registration state.\n"
        "  --help, -h              Show this text.\n"
        "  --                      Treat every following argument as a file path.\n"
        "\n"
        "Diagnostics:\n"
        "  --version               Name, build id, and Qt version.\n"
        "  --print-dpi-awareness   The process DPI awareness context.\n"
        "  --print-args            The argv and QCoreApplication::arguments() lists, side by side.\n"
        "  --crash-test            Force a crash to exercise the minidump path (debug builds only).\n"
        "\n"
        "Exit codes:\n"
        "  0   success\n"
        "  2   usage error -- an unknown option, or an option missing its value\n");
}

int exitCodeFor(const core::Error& error)
{
    // A function rather than a constant because Stage 14's verbs bring failures that are NOT usage
    // errors -- a batch job that ran and reported failures is a different exit code from a mistyped
    // switch, and a caller's script must be able to tell them apart.
    switch (error.code) {
    case ErrorCode::InvalidArgument:
        return static_cast<int>(ExitCode::UsageError);
    default:
        // Stage 1's parser cannot produce anything else. Arriving here means a new failure
        // category was added without an exit code, and a usage error is the least misleading
        // answer until Stage 14 gives it its own.
        return static_cast<int>(ExitCode::UsageError);
    }
}

} // namespace CommandLine
} // namespace rawpage::app
