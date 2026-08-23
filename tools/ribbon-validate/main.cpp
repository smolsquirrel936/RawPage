// tools/ribbon-validate -- the build-time half of decision D1-8.
//
// ---------------------------------------------------------------------------------------------
// THIS IS NOT A GATE, AND THE DISTINCTION IS LOAD-BEARING.
//
// RawPage has THREE live gates -- layer-lint, license-audit, i18n-lint -- and `checks` runs three
// and says three. CLAUDE.md is explicit that if it ever claims four, that is the bug. This program
// is deliberately not one of them:
//
//   * a gate answers a question about the whole tree and runs on a bare Linux runner with no Qt,
//     BEFORE anything is compiled;
//   * this answers one question about one resource, needs the same JSON parser the product uses,
//     and is a step in building ui/. It is wired as a build dependency of rawpage_ui, so a bad
//     res/ribbon.json fails the BUILD -- which is what D1-8 asks for and what A-51 checks.
//
// Wiring it into `checks` would have been the smaller CMake diff and would have made the gate count
// a lie, and quietly moved a Qt dependency into the job whose whole value is not having one.
//
// IT SHARES THE PRODUCT'S PARSER. ui/shell/RibbonModel.cpp is COMPILED INTO this executable rather
// than reimplemented -- see that header. A validator with its own parser can certify a file the
// runtime then reads differently, which is worse than no validator at all.
// ---------------------------------------------------------------------------------------------

#include "ui/shell/RibbonModel.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTextStream>

namespace {

using namespace rawpage::ui;

int usage(QTextStream& err)
{
    err << "usage: ribbon-validate <path-to-ribbon.json> [--explain]\n"
        << "\n"
        << "Validates a declarative ribbon resource against core/cmd/ActionId.def (decision D1-8).\n"
        << "Exit 0 when it is sound, 1 when it is not, 2 on a usage error.\n";
    return 2;
}

void explain(QTextStream& out)
{
    // The same courtesy the three gates extend: a tool that enforces a rule should be able to state
    // the rule. `--explain` is how anyone asks "what would this refuse?" without reading its source.
    out << "ribbon-validate enforces, on res/ribbon.json:\n"
        << "  schema-version    the file declares schemaVersion " << ribbon::kSchemaVersion << "\n"
        << "  unknown-action    every control names an id declared in core/cmd/ActionId.def,\n"
        << "                    resolved through RAWPAGE_ACTION_ALIAS rows so a retired spelling\n"
        << "                    keeps building exactly as a user's saved shortcut keeps resolving\n"
        << "  duplicate-place   no action is placed in two groups, anywhere in the file\n"
        << "  empty-group       no group is declared with no controls\n"
        << "  title-key         every tab and group title is a catalog key, not a sentence:\n"
        << "                    \"ribbon.tab.*\" and \"ribbon.group.*\"\n"
        << "  identity          tab ids and group ids are each unique\n"
        << "\n"
        << "None of these is escapable. There is no inline waiver marker, because unlike\n"
        << "i18n-lint's literal heuristic every rule here is decidable: a control either names a\n"
        << "declared action or it does not.\n";
}

} // namespace

int main(int argc, char** argv)
{
    // QCoreApplication is constructed for QString's sake rather than for an event loop, and argv is
    // read only for a SWITCH -- never for a path. See app/main.cpp and issue I-04: argv is narrow
    // and lossy for anything the process ANSI code page cannot represent. This tool is invoked by
    // the build system with an ASCII path under the source tree, and layer-lint's `argv-path-read`
    // rule allows argv only in app/main.cpp, so the path below goes through
    // QCoreApplication::arguments() like everything else in the product.
    QCoreApplication application(argc, argv);

    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList arguments = QCoreApplication::arguments();

    if (arguments.contains(QStringLiteral("--explain"))) {
        explain(out);
        return 0;
    }
    if (arguments.size() != 2) return usage(err);

    const QString    path = arguments.at(1);
    const QFileInfo  info(path);
    QFile            file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        err << "ribbon-validate: cannot read " << path << ": " << file.errorString() << "\n";
        return 1;
    }

    const QByteArray          bytes  = file.readAll();
    const ribbon::ParseResult parsed = ribbon::parse(bytes);

    if (!parsed.ok()) {
        // file:line: message, so the failure lands in an editor's error list and in CI's annotator
        // without either being taught a new format. Every diagnostic is printed, not just the
        // first: fixing a stale resource one build at a time is how a mechanism meant to be used
        // daily becomes one people route around.
        for (const ribbon::Diagnostic& diagnostic : parsed.diagnostics)
            err << diagnostic.formatted(info.absoluteFilePath()) << "\n";

        err << "ribbon-validate: " << parsed.diagnostics.size() << " problem(s) in "
            << info.fileName() << " -- the ribbon is built entirely from this file, so a bad row "
            << "here is a missing button with no other symptom\n";
        return 1;
    }

    out << "ribbon-validate: clean -- " << parsed.spec.tabs.size() << " tabs, "
        << parsed.spec.catalogKeys().size() - parsed.spec.tabs.size() << " groups, "
        << parsed.spec.controlCount() << " controls\n";
    return 0;
}
