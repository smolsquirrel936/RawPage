// RawPage's entry point.
//
// ORDER IS THE WHOLE POINT OF THIS FILE. Three things must happen before Qt exists, and each
// of them is silently wrong -- not loudly broken -- if it happens afterwards:
//
//   1. Per-monitor-v2 DPI awareness. Qt reads the process awareness once, when the platform
//      integration is created, and caches what it finds. Set it after that and every scaling
//      case in the guide (M-02) fails for a reason nobody can see in the code.
//   2. The unhandled-exception filter. An exception thrown while Qt is initialising is one of
//      the ones most worth catching, and it is exactly the one a handler installed "once the
//      app is up" misses.
//   3. The log sink, so the two above have somewhere to report to.
//
// After that this file is flow A in Architecture-FileMap.md §5, and nothing more: parse, compose,
// decide whether this process is the one that carries on, show the window, run the event loop.
// Every decision it makes is delegated to a class that can be tested on its own -- the parsing to
// app/CommandLine, the wiring to app/CompositionRoot, the claim to app/SingleInstance -- because
// main() is the one function no test can call.
//
// THIS IS ALSO THE ONE TRANSLATION UNIT ALLOWED TO TOUCH `argv`. tools/layer-lint's
// `argv-path-read` rule says so and is not escapable. See app/CommandLine.h for why -- in one
// line: `argv` is narrow, this machine's code page is 950, and an emoji in a path comes back as
// `?????`.

#include "app/Application.h"
#include "app/CommandLine.h"
#include "app/CompositionRoot.h"
#include "app/CrashHandler.h"
#include "app/LogFile.h"
#include "app/SingleInstance.h"

#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/settings/SettingsSchema.h"

#include "pdf/render/PdfiumInit.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/PageLayoutEngine.h"

#include <windows.h>

#include <QByteArray>
#include <QScreen>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace {

// --- Console output from a -mwindows binary ----------------------------------
// The GUI subsystem means no console is allocated. Two cases have to work:
//   * launched from a console -- AttachConsole(ATTACH_PARENT_PROCESS) borrows the parent's,
//     which is what makes `rawpage.exe --version` readable when a person types it;
//   * output redirected or piped -- STD_OUTPUT_HANDLE is already valid and AttachConsole is
//     neither needed nor harmful.
// Written through WriteFile rather than std::cout because the CRT's stdout was never wired to
// anything in a GUI-subsystem process, and reopening it is more moving parts than this.
void attachParentConsole()
{
    if (GetStdHandle(STD_OUTPUT_HANDLE) == nullptr)
        AttachConsole(ATTACH_PARENT_PROCESS);
}

void writeLine(const QString& text)
{
    const QByteArray utf8 = (text + QLatin1Char('\n')).toUtf8();
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == nullptr || out == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(out, utf8.constData(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

// --- DPI awareness -----------------------------------------------------------
// Per-monitor-v2 rather than per-monitor-v1: v2 is what makes non-client area, dialogs and
// menus scale with the window when it moves between monitors, which is the half of M-02 that
// a v1 process fails at while looking correct in a screenshot.
//
// Called directly rather than through GetProcAddress. The pin (Tech-Stack-Plan.md 2) is
// Windows 10 1703 or newer for the whole product, and a dynamic lookup here would turn a
// support-matrix decision into a silent runtime fallback.
bool setDpiAwareness()
{
    return SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
}

QString dpiAwarenessName()
{
    const DPI_AWARENESS_CONTEXT context = GetThreadDpiAwarenessContext();
    if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        return QStringLiteral("PerMonitorAwareV2");
    if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE))
        return QStringLiteral("PerMonitorAware");
    if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE))
        return QStringLiteral("SystemAware");
    if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_UNAWARE))
        return QStringLiteral("Unaware");
    return QStringLiteral("Unknown");
}

// --- The log sink ------------------------------------------------------------
// core/ defines no sink because core/ may not write files (tools/layer-lint enforces it), so
// app/ supplies one. This is the console half: the debugger channel plus stderr, installed before
// anything else so that CrashHandler::install()'s lines are not swallowed. The rolling file under
// %LOCALAPPDATA% and its `logging/retainFiles` reader arrived at T14 and are `app/LogFile`; they
// are composed later, because the retention count is a setting and Settings does not exist yet.
//
// Returned rather than only installed, because app/CompositionRoot fans it out -- the subsystem
// filter from `logging/subsystems` (T5's key, applied at T8), then this, then the rolling file and
// the in-memory buffer the log pane reads (both T14) -- and has to be able to put this one back on
// the way out.
rawpage::core::LogSink makeConsoleLogSink()
{
    return [](const rawpage::core::LogRecordData& record) {
        const QString line = QStringLiteral("[%1][%2] %3")
            .arg(QLatin1String(rawpage::core::logLevelName(record.level)),
                 QLatin1String(rawpage::core::logSubsystemName(record.subsystem)),
                 record.message);
        OutputDebugStringW(reinterpret_cast<const wchar_t*>((line + QLatin1Char('\n')).utf16()));

        const QByteArray utf8 = (line + QLatin1Char('\n')).toUtf8();
        HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
        if (err != nullptr && err != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(err, utf8.constData(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        }
    };
}

// --- Diagnostic switches -----------------------------------------------------
// app/CommandLine owns the VOCABULARY (`CommandLine::diagnosticSwitches()`); this function owns
// the ANSWERS, because each of these reports a fact about the process rather than about the
// product and must be available before anything is composed.
//
// Returns true when the switch was handled and the process should exit with `exitCode`.
bool handleDiagnosticSwitch(const QStringList& arguments, int argc, char** argv, int& exitCode)
{
    exitCode = 0;

    if (arguments.contains(QStringLiteral("--version"))) {
        writeLine(QStringLiteral("RawPage %1").arg(QCoreApplication::applicationVersion()));
        const QString id = rawpage::app::CrashHandler::buildId();
        writeLine(QStringLiteral("build-id %1").arg(id.isEmpty() ? QStringLiteral("(none)") : id));
        writeLine(QStringLiteral("Qt %1").arg(QLatin1String(qVersion())));
        // The rasterizer, because "which engine did this bug report come from" is the second
        // question anyone asks about a rendering defect and the first one nobody can answer from a
        // screenshot. It is the BUILD-TIME PIN that cmake/PdfiumAssert.cmake verified against
        // third_party/pdfium/VERSION -- the FPDF_ C API exposes no version call at all -- and the
        // line says so, because the one reader who cares about this number is usually the one
        // wondering whether the pdfium.dll beside the executable is still the one we built against.
        writeLine(QStringLiteral("PDFium %1 (pinned at build)")
                      .arg(rawpage::pdf::render::version()));
        return true;
    }

    if (arguments.contains(QStringLiteral("--print-dpi-awareness"))) {
        writeLine(dpiAwarenessName());
        return true;
    }

    // The D1-3 measurement (guide case A-05). It prints BOTH lists, because the question is
    // not "does Qt have my arguments" but "does the narrow CRT argv lose them where
    // QCoreApplication::arguments() does not" -- and only a side-by-side answers that. Whether
    // -municode is needed is decided by reading this output, not by assuming either way.
    if (arguments.contains(QStringLiteral("--print-args"))) {
        // Three separate facts, kept separate on purpose. The first draft of this case
        // printed `_UNICODE` and read "yes" -- but Qt6::Core puts UNICODE/_UNICODE in its
        // INTERFACE compile definitions, so that probe measures Qt's preprocessor state and
        // says nothing whatever about the linker flag. RAWPAGE_MUNICODE comes from
        // cmake/RawPageWarnings.cmake, which owns the flag, so it cannot drift from it.
        writeLine(QStringLiteral("municode=%1")
            .arg(RAWPAGE_MUNICODE ? QStringLiteral("on") : QStringLiteral("off")));
        writeLine(QStringLiteral("entryPoint=main(int,char**)"));
        writeLine(QStringLiteral("_UNICODE=%1 (defined by Qt6::Core, unrelated to -municode)")
            .arg(
#ifdef _UNICODE
                QStringLiteral("defined")
#else
                QStringLiteral("undefined")
#endif
            ));

        // The whole result turns on this number: argv is narrow, so the CRT transcodes the
        // wide command line into the ACP, and anything the ACP cannot represent is replaced
        // by '?' -- lossily and irreversibly. Printing the code page is what makes this run
        // reproducible on a machine with a different one.
        writeLine(QStringLiteral("activeCodePage=%1").arg(GetACP()));
        writeLine(QStringLiteral("argv.count=%1").arg(argc));
        for (int i = 0; i < argc; ++i) {
            // fromLocal8Bit, not fromUtf8: argv arrives in the process's active code page, and
            // decoding it as UTF-8 would hide the very mangling this case exists to detect.
            writeLine(QStringLiteral("argv[%1]=%2").arg(i).arg(QString::fromLocal8Bit(argv[i])));
        }
        writeLine(QStringLiteral("qt.count=%1").arg(arguments.size()));
        for (int i = 0; i < arguments.size(); ++i)
            writeLine(QStringLiteral("qt[%1]=%2").arg(i).arg(arguments.at(i)));
        return true;
    }

    if (arguments.contains(QStringLiteral("--crash-test"))) {
#ifdef NDEBUG
        // A shipping binary that will crash on request is a denial-of-service switch, so the
        // release build does not merely refuse -- it has no path to the fault at all.
        writeLine(QStringLiteral("--crash-test is a debug-only switch"));
        exitCode = 2;
        return true;
#else
        rawpage::app::CrashHandler::forceCrashForTesting();
#endif
    }

    return false;
}

} // namespace

// A plain narrow main(), deliberately -- decision D1-3. cmake/RawPageWarnings.cmake carries the
// -municode half of the reasoning; --print-args above is the measurement that settles it.
int main(int argc, char** argv)
{
    using namespace rawpage;

    // (1) Before Qt. See the file header for why the order is load-bearing.
    const bool dpiSet = setDpiAwareness();

    // (3) before (2), so that (2) has somewhere to report to.
    attachParentConsole();
    const core::LogSink consoleSink = makeConsoleLogSink();
    core::setLogSink(consoleSink);

    // Stage 3 replaces the default no-op with RecoveryJournal::flushAll(). Until then the hook
    // is installed, invoked, and records that it is a no-op -- guide case A-66.
    app::CrashHandler::install();

    if (!dpiSet) {
        RAWPAGE_LOG_WARNING(core::LogSubsystem::Ui)
            << "SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2) failed; scaling will be wrong";
    }

    app::Application application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("RawPage"));
    QCoreApplication::setApplicationVersion(QStringLiteral(RAWPAGE_VERSION));

    // THE ORGANIZATION NAME IS DELIBERATELY NOT SET, and this is the load-bearing half of issue
    // I-13. QStandardPaths::AppDataLocation is %APPDATA%\<org>\<app>, so setting an organization
    // of "RawPage" alongside an application of "RawPage" puts every preference in
    // %APPDATA%\RawPage\RawPage -- which is what the first end-to-end run of the composition root
    // actually produced, against the %APPDATA%\RawPage\settings.ini that Implementation-Plan.md's
    // Stage 1 section specifies. We ARE the organization; there is no separate vendor to name, and
    // nothing else in the project reads it. If a vendor name is ever wanted (Stage 15 packaging is
    // the plausible caller), it arrives with a migration for the settings path, not on its own.

    int exitCode = 0;
    if (handleDiagnosticSwitch(QCoreApplication::arguments(), argc, argv, exitCode))
        return exitCode;

    // --- Parse -------------------------------------------------------------------------------
    // QCoreApplication::arguments() and NOTHING ELSE -- issue I-04. `argv` above is read by the
    // --print-args measurement and by QApplication's own constructor, and by nothing else in the
    // program; tools/layer-lint's `argv-path-read` rule keeps it that way.
    const core::Result<app::Intent> parsed = app::CommandLine::parse(QCoreApplication::arguments());
    if (!parsed) {
        // A usage error has to be readable before we know what `--lang` said -- the line that
        // would have told us is the line that failed to parse. So the translators are loaded here
        // with no explicit request, which gives `en` plus whatever the SYSTEM locale offers: the
        // user still gets their own language where the catalog has it. Reporting the raw
        // `error.cli.unknownOption` key instead would be the exact failure mode T6 designed the
        // catalog to make visible, arriving through the one path that runs before the catalog.
        application.loadTranslators(QString(), QString());
        writeLine(QStringLiteral("rawpage: %1")
                      .arg(ui::catalog::translateError(QString(parsed.error().messageKey),
                                                       parsed.error().context)));
        writeLine(app::CommandLine::helpText());
        return app::CommandLine::exitCodeFor(parsed.error());
    }
    const app::Intent intent = parsed.value();

    if (intent.kind == app::Intent::Kind::ShowHelp) {
        writeLine(app::CommandLine::helpText());
        return static_cast<int>(app::ExitCode::Ok);
    }

    // --- Compose -----------------------------------------------------------------------------
    app::CompositionRoot::Options options;
    options.baseSink = consoleSink;

    // The rolling file half of the sink, promised to T14 by the comment on `makeConsoleLogSink`
    // above since T1. Only this call site asks for one: an empty directory means no file, so a
    // test or a diagnostic tool that builds a CompositionRoot does not write into a profile.
    options.logDirectory = app::LogFile::defaultDirectory();
    app::CompositionRoot root(std::move(options));

    // --- One instance, or two? ---------------------------------------------------------------
    app::SingleInstance instance(
        root.platform().singleInstanceBus.get(),
        app::SingleInstance::scopeIdForExecutable(QCoreApplication::applicationFilePath()));

    // A DIAGNOSTIC THAT COMPOSES AND EXITS NEVER TOUCHES THE BUS (`Intent::composesAndExits`,
    // S2 issue I-27). Claiming it while a window is open makes THIS process the secondary: it
    // forwards, returns 0, and prints nothing -- and exiting 0 is also what a successful
    // `--print-screen-metrics` does, so the caller cannot tell the two apart. `ui.screen_metrics_
    // invariant` (A-84) then fails on any machine where somebody has the product open, which is
    // how this was found: the M-05 walk was running in another window.
    std::optional<app::SingleInstance::Role> role;
    if (!intent.composesAndExits()) {
        const core::Result<app::SingleInstance::Role> claimed =
            instance.claim(root.settings().get(core::keys::general::singleInstance));
        if (!claimed) {
            RAWPAGE_LOG_ERROR(core::LogSubsystem::Ui)
                << "single instance: claim failed --" << claimed.error().messageKey
                << claimed.error().context;
        } else if (claimed.value() == app::SingleInstance::Role::Secondary) {
            // Forward and go. The bus grants the primary the right to take the foreground on the
            // way out; see decision D1-11 for why that has to happen from THIS process.
            const core::Status forwarded = instance.forward(intent.files);
            if (!forwarded) {
                RAWPAGE_LOG_ERROR(core::LogSubsystem::Ui)
                    << "single instance: handoff failed --" << forwarded.error().messageKey;
            }
            return static_cast<int>(app::ExitCode::Ok);
        } else {
            role = claimed.value();
        }
    }

    // --- Language ----------------------------------------------------------------------------
    application.loadTranslators(intent.locale, root.settings().get(core::keys::language::uiLocale));

    if (intent.dumpUiStrings) {
        // Guide case A-72, and a headless smoke test in its own right: it composes every service,
        // loads the catalogs and exits 0 without ever creating a window.
        for (const QString& catalogPath : application.loadedCatalogs())
            writeLine(QStringLiteral("# catalog\t%1").arg(catalogPath));
        writeLine(QStringLiteral("# locale\t%1").arg(application.activeLocale()));
        for (const QString& line : ui::catalog::dumpStrings()) writeLine(line);
        return static_cast<int>(app::ExitCode::Ok);
    }

    if (intent.printScreenMetrics) {
        // ISSUE I-20's REMEDY, and it is a diagnostic rather than a test because no test can be
        // one: `tests/ui` runs under `offscreen`, where there is a single fake screen and no
        // topology, and `platform.displaytopology` (A-60) measures the topology with no screen
        // beside it. The join between them -- `QScreen` to `MonitorInfo` -- is reachable only on a
        // real desktop, and it was WRONG on every real desktop for the whole of Stage 2 while both
        // of those suites stayed green.
        //
        // It prints BOTH SIDES and then the resolved answer, deliberately: a line saying only
        // "physical dpi 163" would look correct in exactly the case that was broken, because the
        // fallback also produces a plausible number. What makes a mismatch visible is seeing the
        // key that was matched on, the row it found, and WHICH SOURCE won.
        const platform::IDisplayTopology* topology = root.platform().displayTopology.get();

        writeLine(QStringLiteral("# topology"));
        if (topology == nullptr) {
            writeLine(QStringLiteral("(no IDisplayTopology in this bundle)"));
        } else if (const core::Result<std::vector<platform::MonitorInfo>> monitors =
                       topology->monitors()) {
            for (const platform::MonitorInfo& monitor : monitors.value()) {
                writeLine(QStringLiteral("monitor	%1	%2	%3x%4+%5+%6	scale %7	dpi %8%9")
                              .arg(monitor.id, monitor.friendlyName)
                              .arg(monitor.width)
                              .arg(monitor.height)
                              .arg(monitor.x)
                              .arg(monitor.y)
                              .arg(monitor.scaleFactor)
                              .arg(monitor.physicalDpi)
                              .arg(monitor.primary ? QStringLiteral("	primary") : QString()));
            }
        } else {
            writeLine(QStringLiteral("(monitors() failed: %1)").arg(monitors.error().messageKey));
        }

        writeLine(QStringLiteral("# screens, and what metricsFor() resolves for each"));
        for (const QScreen* screen : QGuiApplication::screens()) {
            const ui::ScreenMetrics metrics = ui::metricsFor(screen, topology);
            writeLine(QStringLiteral("screen	%1	dpr %2	qtPhysDpi %3	-> scale %4	physDpi %5"
                                     "	source %6")
                          .arg(screen->name())
                          .arg(screen->devicePixelRatio())
                          .arg(screen->physicalDotsPerInch())
                          .arg(metrics.scale.factor)
                          .arg(metrics.physical.dpi)
                          .arg(QLatin1String(ui::ScreenMetrics::sourceName(metrics.source))));
        }
        return static_cast<int>(app::ExitCode::Ok);
    }

    // --- The window --------------------------------------------------------------------------
    ui::MainWindow window(root.services());

    if (intent.listActions) {
        // STAGE 1 GUIDE CASE A-25, and it needs the window rather than only the services: commands
        // are registered in `MainWindow`'s constructor, once per registry (convention 14). Taken
        // before one exists, this dump would report every id unregistered -- a diagnostic that is
        // confidently wrong, which is worse than one that does not exist.
        //
        // WHAT IT PRINTS, AND WHAT A-25 ASKED FOR THAT IS NOT HERE. The case says *"every id listed
        // with its state: registered, or declared-with-owning-stage"*. The state is here; the
        // OWNING STAGE is not, and that half of the criterion is CORRECTED rather than quietly
        // dropped -- the same call T7 made about A-18, T15 about A-65 and T16 about A-67.
        //
        // `ActionId.def` carries no stage column, and adding one to 361 rows would put a second
        // copy of the roadmap in a table that no reader of `Implementation-Plan.md` would ever
        // think to update -- rule 2's *a fact lives in exactly one place*, broken 361 times. Stage
        // ownership lives in `Implementation-Plan.md` and `Architecture-FileMap.md`, and what this
        // dump is actually for is the other direction: it is the list that stops a later stage
        // silently forgetting to register something it declared, and for that the state and the
        // count are the whole of it.
        //
        // THE CHORDS, added 2026-09-14 for Stage 2 guide case A-72, which asked for them and found
        // the column missing. They are the EFFECTIVE chords -- `KeyBindingStore::bindingsFor`, the
        // same merged view the Shortcuts page shows and the keyboard resolves, so a §8 default the
        // user has replaced is absent and their replacement is present, marked `[user]`. The §8
        // table is `Features.md`'s to state; what a support transcript needs is what a key press
        // on THIS machine does.
        //
        // One trailing field per binding, `Context:Chord`, rather than one column joined by a
        // separator: §8's own chords contain a space (`Page Down`), a comma (`Ctrl+,`) and a
        // semicolon (`Ctrl+;`), and no key is named with a tab. `-` when an action has none.
        //
        // Tab-separated and untranslated: it is a developer diagnostic, and
        // `tools/i18n-lint/rules.json` puts console output out of scope because a translated
        // support transcript cannot be read back.
        const core::ActionRegistry& registry = root.services().actions;
        const core::KeyBindingStore& bindings = root.services().keyBindings;
        const std::vector<core::ActionRegistry::ActionState> states = registry.allActions();

        std::size_t registered = 0;
        for (const core::ActionRegistry::ActionState& state : states)
            if (state.registered) ++registered;

        std::vector<QStringList> chords(core::kActionCount);
        for (std::size_t c = 0; c < core::kKeyContextCount; ++c) {
            const auto context = static_cast<core::KeyContext>(c);
            const std::string_view contextName = core::keyContextName(context);
            for (const core::KeyBinding& binding : bindings.bindingsFor(context)) {
                if (!binding.action) continue;   // widget-native: no action to list it under
                const std::optional<core::Chord> chosen =
                    bindings.overrideChordFor(*binding.action, context);
                const bool user = chosen && *chosen == binding.chord;
                chords[static_cast<std::size_t>(*binding.action)].append(
                    QStringLiteral("%1:%2%3")
                        .arg(QString::fromUtf8(contextName.data(),
                                               static_cast<qsizetype>(contextName.size())),
                             QString::fromStdString(binding.chord.toString()),
                             user ? QStringLiteral("[user]") : QString()));
            }
        }

        writeLine(QStringLiteral("# actions\t%1 declared\t%2 registered\t%3 unregistered")
                      .arg(states.size())
                      .arg(registered)
                      .arg(states.size() - registered));
        writeLine(QStringLiteral("# columns\tstate\tstringId\tdefaultContext\tlabelKey\tchords..."));

        for (const core::ActionRegistry::ActionState& state : states) {
            const QStringList& actionChords = chords[static_cast<std::size_t>(state.id)];
            writeLine(QStringLiteral("%1\t%2\t%3\t%4\t%5")
                          .arg(state.registered ? QStringLiteral("registered")
                                                : QStringLiteral("declared"))
                          .arg(QString::fromUtf8(core::actionStringId(state.id).data(),
                                                 static_cast<qsizetype>(
                                                     core::actionStringId(state.id).size())))
                          .arg(QString::fromUtf8(
                              core::keyContextName(core::actionDefaultContext(state.id)).data(),
                              static_cast<qsizetype>(
                                  core::keyContextName(core::actionDefaultContext(state.id))
                                      .size())))
                          .arg(QString::fromUtf8(core::actionLabelKey(state.id).data(),
                                                 static_cast<qsizetype>(
                                                     core::actionLabelKey(state.id).size())))
                          .arg(actionChords.isEmpty() ? QStringLiteral("-")
                                                      : actionChords.join(QLatin1Char('\t'))));
        }
        return static_cast<int>(app::ExitCode::Ok);
    }

    if (role == app::SingleInstance::Role::Primary) {
        const core::Status listening = instance.listen(
            [&window](const QStringList& files) { window.openHandoff(files); });
        if (!listening) {
            RAWPAGE_LOG_ERROR(core::LogSubsystem::Ui)
                << "single instance: this process holds the claim but cannot listen --"
                << listening.error().messageKey;
        }
    }

    window.show();
    window.openFiles(intent.files);

    return application.exec();
}
