// Parsing the command line into an INTENT -- and the one place the argv rule is written down.
//
// ---------------------------------------------------------------------------------------------
// THE ARGV RULE (issue I-04, Stage1-AppShell-Testing-Guide.md §8; verdict recorded in
// Implementation-Plan.md's Stage 1 status block).
//
// `parse()` takes a QStringList and there is NO overload taking argc/argv. That is not an
// oversight and it is not stylistic. T1 measured it: this machine's ANSI code page is 950, the CRT
// transcodes the wide command line into that code page to build `argv`, and anything the code page
// cannot represent is replaced by '?' -- lossily and irreversibly. An emoji path arrived as
// `report-?????.pdf` in `argv` while `QCoreApplication::arguments()`, which Qt sources from
// GetCommandLineW(), was byte-perfect.
//
// So the rule for the whole product is: NOTHING MAY READ A PATH OUT OF ARGV. Three mechanisms hold
// it, because one is a convention and conventions decay:
//
//   1. There is no argc/argv overload to call -- tests/unit/compile/argv-parse.cpp proves that a
//      caller who tries does not compile.
//   2. tools/layer-lint carries an `argv-path-read` token rule, allowed only in app/main.cpp,
//      which is the single translation unit that legitimately holds argv (it hands it to
//      QApplication and prints it for the --print-args measurement). escapable: false.
//   3. This comment, which is the part that explains the other two.
// ---------------------------------------------------------------------------------------------
//
// Stage 1 parses the open-with-file case and --lang, and nothing else. The full CLI verb set of
// Features.md §9.6 -- and the headless batch path that runs without ever creating a window -- is
// Stage 14. What exists now is the SHAPE: a pure function from a QStringList to an intent, with no
// filesystem access, no Qt GUI type, and no side effect, which is what lets tst_commandline live in
// the headless suite (Stage1-AppShell-Plan.md §5.7).

#ifndef RAWPAGE_APP_COMMANDLINE_H
#define RAWPAGE_APP_COMMANDLINE_H

#include "core/Result.h"

#include <QString>
#include <QStringList>

namespace rawpage::app {

// The process exit codes Stage 1 defines. Stage 14 extends this table; it does not renumber it,
// because an exit code is a public interface the moment a script branches on it.
enum class ExitCode : int {
    Ok         = 0,
    UsageError = 2,   // an unknown option, or an option missing its value
};

struct Intent {
    enum class Kind {
        // Build the services and show a window. `files` may be empty.
        RunGui,

        // A switch app/main.cpp answers for itself, BEFORE Qt is composed, because it reports a
        // fact about the process rather than about the product: --version, --print-dpi-awareness,
        // --print-args, --crash-test. The parser recognises them so that they are not reported as
        // unknown options; it does not act on them.
        Diagnostic,

        ShowHelp,
    };

    Kind        kind = Kind::RunGui;

    // Paths exactly as the user wrote them, in order, undecoded and unchecked. Whether a path
    // exists -- and whether its bytes are a PDF -- is decided far from here: existence by
    // ui/shell/MainWindow, and the bytes by IDocumentBackend at Stage 2 (decision D1-2).
    QStringList files;

    // --lang <code>. Empty means "follow language/uiLocale, then the system locale".
    QString     locale;

    // --dump-ui-strings: print the shell's resolved catalog strings and exit. The mechanical half
    // of guide case A-72, which asks whether --lang zh-TW actually LOADS a catalog. Whether the
    // Chinese is any good is manual case M-06 and this switch cannot tell you.
    bool        dumpUiStrings = false;

    // Which diagnostic, when kind == Diagnostic.
    QString     diagnostic;
};

namespace CommandLine {

// PASS QCoreApplication::arguments() AND NOTHING ELSE. See the file header.
//
// The first element is the program name and is skipped; an empty list is legal and yields a plain
// RunGui intent, because a process launched with no arguments at all is the ordinary case.
core::Result<Intent> parse(const QStringList& arguments);

// The switches main.cpp answers before composition. Data rather than a chain of comparisons, so
// that main.cpp and the parser cannot disagree about what the vocabulary is.
const QStringList& diagnosticSwitches();

// Developer-facing, deliberately untranslated: tools/i18n-lint/rules.json records that console
// diagnostics are out of scope because a translated support transcript cannot be read back.
QString helpText();

int exitCodeFor(const core::Error& error);

} // namespace CommandLine

} // namespace rawpage::app

#endif // RAWPAGE_APP_COMMANDLINE_H
