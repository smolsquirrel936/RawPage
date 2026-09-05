// Stage 1 T8 -- app/CommandLine. Guide cases A-63 and A-64 (Stage1-AppShell-Testing-Guide.md 5.7),
// and the case that closes issue I-04.
//
// WHY THIS LIVES IN THE HEADLESS SUITE despite parsing arguments for a GUI executable
// (Stage1-AppShell-Plan.md 5.7): the parser is a pure function from a QStringList to an intent --
// no filesystem, no Qt GUI type, no side effect -- so tests/unit compiles app/CommandLine.cpp
// directly rather than linking the executable. If that ever stops being possible, the parser has
// grown a dependency it should not have, and THAT is the finding, not the test.
//
// A-64 is asserted by the build rather than from inside this file: rawpage_assert_headless() runs
// over the resolved link closure of this target, so if adding the parser had dragged in
// rawpage_ui or rawpage_platform, configure would have failed before a test ran.

#include <catch2/catch_test_macros.hpp>

#include "app/CommandLine.h"

#include <QString>
#include <QStringList>

using rawpage::app::CommandLine::parse;
using rawpage::core::ErrorCode;

namespace {

// Every real caller passes QCoreApplication::arguments(), whose first element is the program path.
// Building the list that way here rather than omitting element 0 keeps the tests honest about what
// the function is actually handed.
QStringList argumentsOf(const QStringList& tail)
{
    QStringList arguments{QStringLiteral("C:/Program Files/RawPage/rawpage.exe")};
    arguments += tail;
    return arguments;
}

} // namespace

TEST_CASE("an empty argument list is a plain GUI launch", "[commandline]")
{
    // Not an error and not a special case: a process launched with no arguments at all is the
    // ordinary way this application starts.
    const auto parsed = parse({});
    REQUIRE(parsed);
    CHECK(parsed.value().kind == rawpage::app::Intent::Kind::RunGui);
    CHECK(parsed.value().files.isEmpty());
    CHECK(parsed.value().locale.isEmpty());

    const auto justTheProgram = parse(argumentsOf({}));
    REQUIRE(justTheProgram);
    CHECK(justTheProgram.value().files.isEmpty());
}

TEST_CASE("a file path is collected in order and left exactly as written", "[commandline]")
{
    const auto parsed = parse(argumentsOf({QStringLiteral("C:/docs/a.pdf"),
                                           QStringLiteral("relative/b.pdf")}));
    REQUIRE(parsed);
    CHECK(parsed.value().kind == rawpage::app::Intent::Kind::RunGui);
    REQUIRE(parsed.value().files.size() == 2);

    // Not normalised, not made absolute, not checked for existence. Whether the file is there is
    // ui/shell/MainWindow's question and whether its bytes are a PDF is IDocumentBackend's at
    // Stage 2 -- decision D1-2, and guide case A-58 checks the second half of it.
    CHECK(parsed.value().files.at(0) == QStringLiteral("C:/docs/a.pdf"));
    CHECK(parsed.value().files.at(1) == QStringLiteral("relative/b.pdf"));
}

// THIS IS THE CASE THAT CLOSES I-04, and it is worth saying what it does and does not prove.
//
// It proves that the parser is byte-transparent: whatever QCoreApplication::arguments() hands it
// comes out the far side unchanged, including characters no 8-bit code page can represent. It does
// NOT prove that nothing anywhere reads argv -- no runtime test can, because the failure is a
// mangled string that is a perfectly valid string. That half is held by two other mechanisms: the
// tools/layer-lint `argv-path-read` rule (gate case gates.layer_lint.argv_path_read), which is not
// escapable, and the negative-compile fixture unit.commandline.compile.argv_overload, which proves
// there is no argc/argv entry point to call.
TEST_CASE("a path no ANSI code page can represent survives the parser byte for byte", "[commandline]")
{
    // The emoji from the T1 measurement. On this machine (ACP 950) it came back from argv as
    // report-?????.pdf, lossily and irreversibly, while QCoreApplication::arguments() was exact.
    const QString emoji = QStringLiteral("D:/docs/report-\U0001F4C4-final.pdf");
    const QString han   = QStringLiteral("D:/\u6587\u4EF6/\u5831\u544A.pdf");

    const auto parsed = parse(argumentsOf({emoji, han}));
    REQUIRE(parsed);
    REQUIRE(parsed.value().files.size() == 2);
    CHECK(parsed.value().files.at(0) == emoji);
    CHECK(parsed.value().files.at(1) == han);

    // Byte for byte, not merely "looks the same": a comparison of QStrings that had both been
    // through the same lossy transcode would pass while both were wrong.
    CHECK(parsed.value().files.at(0).toUtf8() == emoji.toUtf8());
}

TEST_CASE("lang takes its value as a separate argument or after an equals sign", "[commandline]")
{
    const auto separate = parse(argumentsOf({QStringLiteral("--lang"), QStringLiteral("zh-TW")}));
    REQUIRE(separate);
    CHECK(separate.value().locale == QStringLiteral("zh-TW"));

    const auto inline_ = parse(argumentsOf({QStringLiteral("--lang=zh-TW")}));
    REQUIRE(inline_);
    CHECK(inline_.value().locale == QStringLiteral("zh-TW"));

    // The value is not interpreted here. Mapping zh-TW to a catalog -- and deciding what happens
    // when there is none -- is app/Application's job, because that decision needs the filesystem.
    const auto both = parse(argumentsOf({QStringLiteral("--lang"), QStringLiteral("zh-TW"),
                                         QStringLiteral("C:/docs/a.pdf")}));
    REQUIRE(both);
    CHECK(both.value().locale == QStringLiteral("zh-TW"));
    REQUIRE(both.value().files.size() == 1);
    CHECK(both.value().files.at(0) == QStringLiteral("C:/docs/a.pdf"));
}

TEST_CASE("an option missing its value is an error naming the option", "[commandline]")
{
    for (const QStringList& tail : {QStringList{QStringLiteral("--lang")},
                                    QStringList{QStringLiteral("--lang=")}}) {
        const auto parsed = parse(argumentsOf(tail));
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().code == ErrorCode::InvalidArgument);
        CHECK(parsed.error().messageKey == QLatin1String("error.cli.missingValue"));

        // The context names the option, because "a command-line option needs a value" without
        // saying which one is a diagnostic that costs a support round trip.
        CHECK(parsed.error().context == QStringLiteral("--lang"));
    }
}

TEST_CASE("an unknown option is reported and carries the documented exit code", "[commandline]")
{
    const auto parsed = parse(argumentsOf({QStringLiteral("--langauge"), QStringLiteral("zh-TW")}));
    REQUIRE_FALSE(parsed);
    CHECK(parsed.error().code == ErrorCode::InvalidArgument);
    CHECK(parsed.error().messageKey == QLatin1String("error.cli.unknownOption"));
    CHECK(parsed.error().context == QStringLiteral("--langauge"));

    // Reported rather than ignored on purpose. A mistyped switch silently dropped is, at Stage 14,
    // a batch run that reports success and did something else.
    CHECK(rawpage::app::CommandLine::exitCodeFor(parsed.error())
          == static_cast<int>(rawpage::app::ExitCode::UsageError));
}

TEST_CASE("the diagnostic switches are recognised rather than treated as unknown", "[commandline]")
{
    // app/main.cpp answers these before anything is composed, but the parser has to KNOW them or a
    // switch that works would be reported as a usage error by a parser that never ran.
    for (const QString& diagnostic : rawpage::app::CommandLine::diagnosticSwitches()) {
        const auto parsed = parse(argumentsOf({diagnostic}));
        REQUIRE(parsed);
        CHECK(parsed.value().kind == rawpage::app::Intent::Kind::Diagnostic);
        CHECK(parsed.value().diagnostic == diagnostic);
    }

    // A diagnostic wins over everything else on the line -- --print-args in particular is HANDED
    // paths as its subject, and treating them as files to open would make the measurement open
    // documents.
    const auto mixed = parse(argumentsOf({QStringLiteral("C:/docs/a.pdf"),
                                          QStringLiteral("--print-args")}));
    REQUIRE(mixed);
    CHECK(mixed.value().kind == rawpage::app::Intent::Kind::Diagnostic);
}

TEST_CASE("help is a kind of intent, not an error", "[commandline]")
{
    for (const QString& spelling : {QStringLiteral("--help"), QStringLiteral("-h"),
                                    QStringLiteral("-?")}) {
        const auto parsed = parse(argumentsOf({spelling}));
        REQUIRE(parsed);
        CHECK(parsed.value().kind == rawpage::app::Intent::Kind::ShowHelp);
    }

    // The text names every option the parser accepts. Checked because a help text that has drifted
    // from the parser is worse than none: it sends people to a switch that no longer exists.
    const QString help = rawpage::app::CommandLine::helpText();
    CHECK(help.contains(QStringLiteral("--lang")));
    CHECK(help.contains(QStringLiteral("--dump-ui-strings")));
    CHECK(help.contains(QStringLiteral("--list-actions")));
    for (const QString& diagnostic : rawpage::app::CommandLine::diagnosticSwitches())
        CHECK(help.contains(diagnostic));
}

TEST_CASE("a double dash makes everything after it a file path", "[commandline]")
{
    // A real path can begin with a dash, and a person who has one needs a way to say so that does
    // not depend on us having guessed their filename.
    const auto parsed = parse(argumentsOf({QStringLiteral("--"), QStringLiteral("--lang"),
                                           QStringLiteral("-weird-name.pdf")}));
    REQUIRE(parsed);
    REQUIRE(parsed.value().files.size() == 2);
    CHECK(parsed.value().files.at(0) == QStringLiteral("--lang"));
    CHECK(parsed.value().files.at(1) == QStringLiteral("-weird-name.pdf"));
    CHECK(parsed.value().locale.isEmpty());
}

TEST_CASE("dump-ui-strings is a flag and composes with lang", "[commandline]")
{
    const auto parsed = parse(argumentsOf({QStringLiteral("--lang"), QStringLiteral("zh-TW"),
                                           QStringLiteral("--dump-ui-strings")}));
    REQUIRE(parsed);
    CHECK(parsed.value().dumpUiStrings);
    CHECK(parsed.value().locale == QStringLiteral("zh-TW"));
    CHECK(parsed.value().kind == rawpage::app::Intent::Kind::RunGui);
}

TEST_CASE("list-actions is a flag the parser recognises", "[commandline]")
{
    // STAGE 1 GUIDE CASE A-25's first half, and the reason it is worth a case of its own: that case
    // was recorded at Stage 1 T15 as *NOT RUN -- the switch it names does not exist*, while SIX
    // documents described the diagnostic as though it did. It was half of issue **I-32**. Carried
    // here by D2-10 and built at Stage 2 T18.
    //
    // What this asserts is the PARSE. What the switch prints needs a registry with commands in it,
    // which needs a `MainWindow`, which is `tests/ui`'s and `app/main.cpp`'s -- said here rather
    // than left for a reader to assume this case covers the whole switch (convention 19).
    const auto parsed = parse(argumentsOf({QStringLiteral("--list-actions")}));
    REQUIRE(parsed);
    CHECK(parsed.value().listActions);
    CHECK(parsed.value().kind == rawpage::app::Intent::Kind::RunGui);
    CHECK_FALSE(parsed.value().dumpUiStrings);

    // THE CONTROL: an ordinary run must not set it. Without this the case would pass against a
    // parser that set the flag unconditionally, which is the shape a one-line boolean invites.
    const auto plain = parse(argumentsOf({QStringLiteral("a.pdf")}));
    REQUIRE(plain);
    CHECK_FALSE(plain.value().listActions);

    // ...and it is not a diagnostic. `--version` and friends are answered by main.cpp BEFORE Qt is
    // composed; this one needs the composed application and a window, so it must not be in that
    // list or it would be answered too early and report every id unregistered.
    CHECK_FALSE(rawpage::app::CommandLine::diagnosticSwitches().contains(
        QStringLiteral("--list-actions")));
}
