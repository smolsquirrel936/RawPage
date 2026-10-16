// tools/golden-update -- regenerates the reference rasters in `golden/`, and refuses to do it
// without a stated reason. Stage 2 T17, decisions D2-3 and D2-13
// (`claude-plan/Stage2-Rendering-Plan.md` §3).
//
// ===============================================================================================
// WHY `--reason` IS MANDATORY, WHICH IS THE WHOLE POINT OF THE TOOL
// ===============================================================================================
//
// `Implementation-Plan.md` §2 says baselines are regenerated *"only through `tools/golden-update`
// with a stated reason"*. D2-3 records why that sentence has no teeth on its own: a golden baseline
// is a claim about **one rasterizer**, and the cheapest way to make a rendering regression disappear
// is to re-capture the baseline from the code that broke it. Then the suite is green, the diff shows
// only binary files, and nobody ever learns what moved.
//
// So: no `--reason`, no regeneration. The reason is written into `golden/captured.json` beside the
// images and appended to `golden/UPDATES.md`, and the tool prints a commit-message block ready to
// paste. Putting it in a REVIEWED FILE rather than only in a commit message is deliberate -- a
// commit message can be amended and a squash can lose it, and the reviewer of the next baseline
// change is the person who most needs to read the last one.
//
// ===============================================================================================
// THIS IS NOT A GATE, AND `checks` DOES NOT COUNT IT
// ===============================================================================================
//
// It links Qt, PDFium and the platform layer, so it cannot run on CI's bare `gates` runner, and it
// is not an enforcement rule -- it is the *producer* of the data `tests/golden` enforces against.
// Same standing as `tools/ribbon-validate`, which is a build step rather than a fourth gate, and
// its CMakeLists returns early for the gate and portable configurations for the same reason.
//
// What IS enforced in CTest: `render.golden.*` compares against these baselines, and one of those
// cases drives this executable to prove the refusal above actually happens (guide case A-50). A
// mechanism whose only evidence is its own source comment is the shape Stage 1 kept finding.

#include "tests/golden/AppearanceCases.h"
#include "tests/golden/GoldenSuite.h"

#include "pdf/render/PdfiumInit.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>

namespace {

using namespace rawpage;

QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream& err()
{
    static QTextStream stream(stderr);
    return stream;
}

struct Options {
    QString reason;
    QStringList filters;   // --filter, repeatable: a case matches when ANY of them does
    QString corpusRoot = QStringLiteral(RAWPAGE_CORPUS_DIR);
    QString goldenRoot = QStringLiteral(RAWPAGE_GOLDEN_DIR);
    bool    survey = false;
    bool    appearance = false;   // S6 T8: the appearance cases, not the corpus
    bool    checkManifest = false;
    bool    explain = false;
    bool    help = false;
    QString usageError;
};

Options parse(const QStringList& arguments)
{
    Options options;
    // From `QCoreApplication::arguments()`, never from `argv` -- CLAUDE.md convention 1. The CRT
    // transcodes the wide command line through the ANSI code page (950 on this machine) and
    // replaces what it cannot represent, so a `--reason` in Chinese would arrive as question marks
    // and land in the committed record that way.
    for (int at = 1; at < arguments.size(); ++at) {
        const QString argument = arguments.at(at);
        auto takeValue = [&](const char* name) -> QString {
            if (at + 1 >= arguments.size()) {
                options.usageError = QStringLiteral("%1 needs a value").arg(QLatin1String(name));
                return {};
            }
            return arguments.at(++at);
        };

        if (argument == QLatin1String("--reason"))            options.reason = takeValue("--reason");
        else if (argument == QLatin1String("--filter"))       options.filters << takeValue("--filter");
        else if (argument == QLatin1String("--corpus"))       options.corpusRoot = takeValue("--corpus");
        else if (argument == QLatin1String("--golden"))       options.goldenRoot = takeValue("--golden");
        else if (argument == QLatin1String("--survey"))       options.survey = true;
        else if (argument == QLatin1String("--appearance"))   options.appearance = true;
        else if (argument == QLatin1String("--check-manifest")) options.checkManifest = true;
        else if (argument == QLatin1String("--explain"))      options.explain = true;
        else if (argument == QLatin1String("--help") || argument == QLatin1String("-h"))
            options.help = true;
        else {
            options.usageError = QStringLiteral("unrecognized argument: %1").arg(argument);
            return options;
        }
        if (!options.usageError.isEmpty()) return options;
    }
    return options;
}

void printUsage()
{
    out() << "golden-update -- regenerate the reference rasters in golden/\n\n"
          << "  --reason \"<text>\"   REQUIRED to regenerate. Says why the baselines moved.\n"
          << "                      Written to golden/captured.json and golden/UPDATES.md.\n"
          << "  --filter <text>     only cases whose corpus path contains <text>; repeatable,\n"
          << "                      and a case matches when any of them does\n"
          << "  --survey            measure corpus/ and print a manifest skeleton; writes nothing\n"
          << "  --appearance        the APPEARANCE baselines (tests/golden/AppearanceCases), not the corpus;\n"
          << "                      --filter matches a case's name. Still needs --reason.\n"
          << "  --check-manifest    every corpus file is a case or an excluded-with-a-reason row\n"
          << "  --explain           the thresholds, the case census, and where the baselines live\n"
          << "  --corpus <dir>      override the corpus root\n"
          << "  --golden <dir>      override the golden root\n\n"
          << "Regenerating without --reason is refused (D2-3): a baseline captured from the code\n"
          << "that broke it is a rendering regression laundered into a new reference.\n";
    out().flush();
}

QString manifestPath(const Options& options)
{
    return QDir(options.goldenRoot).filePath(QStringLiteral("manifest.json"));
}

QString capturePath(const Options& options)
{
    return QDir(options.goldenRoot).filePath(QStringLiteral("captured.json"));
}

bool matchesFilter(const golden::Case& item, const Options& options)
{
    // Repeatable since Stage 3 T1, whose six new corpus files share no substring: three filtered
    // runs would have written three UPDATES.md entries for one decision to add baselines.
    if (options.filters.isEmpty()) return true;
    return std::any_of(options.filters.begin(), options.filters.end(),
                       [&item](const QString& filter) {
                           return item.file.contains(filter, Qt::CaseInsensitive);
                       });
}

// ---------------------------------------------------------------------------------------------
// --explain
// ---------------------------------------------------------------------------------------------
int explain(const Options& options)
{
    const golden::Manifest manifest = golden::loadManifest(manifestPath(options));
    if (!manifest.error.isEmpty()) {
        err() << "golden-update: " << manifest.error << "\n";
        err().flush();
        return 1;
    }

    out() << "golden-update --explain\n\n"
          << "  manifest        " << manifestPath(options) << "\n"
          << "  baselines       " << QDir(options.goldenRoot).absolutePath() << "\n"
          << "  corpus          " << QDir(options.corpusRoot).absolutePath() << "\n"
          << "  pdfium          " << pdf::render::version() << " (the build-time pin)\n\n"
          << "  cases           " << manifest.cases.size() << "\n"
          << "  baseline images " << manifest.imageCount() << "\n"
          << "  excluded files  " << manifest.excluded.size() << " (each with a stated reason)\n\n"
          << "  thresholds (D2-13 -- perceptual, and stated rather than implied)\n"
          << "    a pixel differs above a per-channel delta of "
          << manifest.thresholds.channelTolerance << ", and at most "
          << manifest.thresholds.differingFraction * 100.0 << " % of a page may\n"
          << "    a pixel is grossly wrong above " << manifest.thresholds.grossChannelTolerance
          << ", and at most " << manifest.thresholds.grossFraction * 100.0 << " % of a page may\n\n"
          << "  machine-dependent cases (skipped by name where the substitution differs):\n";
    bool any = false;
    for (const golden::Case& item : manifest.cases) {
        if (!item.machineDependent) continue;
        any = true;
        out() << "    " << item.file << " -- " << item.note << "\n";
    }
    if (!any) out() << "    (none)\n";
    out().flush();
    return 0;
}

// ---------------------------------------------------------------------------------------------
// --check-manifest
// ---------------------------------------------------------------------------------------------
// `corpus/make-corpus.py --check-provenance` in a second subject: a corpus file that is neither a
// baseline case nor an explicitly excluded one is a silent gap, and the file most likely to fall
// into it is the one somebody adds three stages from now.
int checkManifest(const Options& options)
{
    const golden::Manifest manifest = golden::loadManifest(manifestPath(options));
    if (!manifest.error.isEmpty()) {
        err() << "golden-update: " << manifest.error << "\n";
        err().flush();
        return 1;
    }

    const QStringList files = golden::corpusFiles(options.corpusRoot);
    if (files.isEmpty()) {
        err() << "golden-update: no PDFs under " << options.corpusRoot << "\n";
        err().flush();
        return 1;
    }

    QStringList unaccounted;
    for (const QString& file : files) {
        if (manifest.find(file) != nullptr) continue;
        const bool excluded =
            std::any_of(manifest.excluded.begin(), manifest.excluded.end(),
                        [&file](const golden::Exclusion& row) { return row.file == file; });
        if (!excluded) unaccounted << file;
    }

    QStringList phantom;
    for (const golden::Case& item : manifest.cases)
        if (!files.contains(item.file)) phantom << item.file;
    for (const golden::Exclusion& row : manifest.excluded)
        if (!files.contains(row.file)) phantom << row.file;

    for (const QString& file : unaccounted)
        err() << "golden-update: " << file
              << " has no baseline case and no stated exclusion\n";
    for (const QString& file : phantom)
        err() << "golden-update: manifest names " << file << ", which is not in the corpus\n";

    if (!unaccounted.isEmpty() || !phantom.isEmpty()) {
        err().flush();
        return 1;
    }
    out() << "golden-update: " << files.size() << " corpus files, "
          << manifest.cases.size() << " with baselines and " << manifest.excluded.size()
          << " excluded with a reason\n";
    out().flush();
    return 0;
}

// ---------------------------------------------------------------------------------------------
// --survey
// ---------------------------------------------------------------------------------------------
// Prints what a manifest COULD say, measured by opening every corpus file. It writes nothing: the
// manifest stays hand-authored and reviewed, and this exists so that authoring it is measurement
// rather than guesswork. A generated manifest would be a second copy of the corpus with no
// reviewer, which is the shape convention 17 exists to prevent.
int survey(const Options& options)
{
    golden::EngineSession session;
    if (!session.isReady()) {
        err() << "golden-update: " << session.error() << "\n";
        err().flush();
        return 1;
    }

    out() << "// measured " << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n";
    for (const QString& file : golden::corpusFiles(options.corpusRoot)) {
        golden::Case probe;
        probe.file = file;
        probe.pages = {0};
        probe.dpi = {96};
        if (file.startsWith(QLatin1String("encrypted/"))) probe.password = QStringLiteral("user");

        const golden::RenderOutcome outcome =
            golden::renderPage(options.corpusRoot, probe, 0, 96);
        if (!outcome.ok) {
            out() << "  // " << file << " -- " << outcome.error << "\n";
            continue;
        }
        // The PAGE COUNT is printed because choosing which pages to capture is the one authoring
        // decision the manifest cannot be written without, and guessing it is how a multi-page
        // fixture ends up with a baseline only for page 0 -- which in a generated corpus is
        // usually the least interesting page it has.
        out() << QStringLiteral(
                     "  { \"file\": \"%1\", \"pages\": [0] },   // %2 page(s), %3x%4 at 96 dpi\n")
                     .arg(file)
                     .arg(golden::pageCount(options.corpusRoot, probe))
                     .arg(outcome.image.width)
                     .arg(outcome.image.height);
        out().flush();
    }
    out().flush();
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Regeneration
// ---------------------------------------------------------------------------------------------
bool appendUpdateLog(const Options& options, const QString& stamp, int images)
{
    const QString path = QDir(options.goldenRoot).filePath(QStringLiteral("UPDATES.md"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        err() << "golden-update: cannot append to " << path << ": " << file.errorString() << "\n";
        return false;
    }
    QTextStream stream(&file);
    stream << "\n## " << stamp << "\n\n"
           << "- **PDFium** " << pdf::render::version() << "\n"
           << "- **Images regenerated** " << images;
    if (!options.filters.isEmpty())
        stream << " (filtered on `" << options.filters.join(QStringLiteral("`, `")) << "`)";
    stream << "\n- **Reason** " << options.reason << "\n";
    return true;
}

int regenerate(const Options& options)
{
    const golden::Manifest manifest = golden::loadManifest(manifestPath(options));
    if (!manifest.error.isEmpty()) {
        err() << "golden-update: " << manifest.error << "\n";
        err().flush();
        return 1;
    }

    golden::EngineSession session;
    if (!session.isReady()) {
        err() << "golden-update: " << session.error() << "\n";
        err().flush();
        return 1;
    }

    // The PREVIOUS capture log, so that a filtered run keeps the records it did not touch. A run
    // over one file must not silently drop the metadata of the other sixty.
    golden::CaptureLog log = golden::loadCaptureLog(capturePath(options));
    log.error.clear();

    const QString stamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    log.pdfiumVersion = pdf::render::version();
    log.capturedUtc = stamp;
    log.reason = options.reason;

    int written = 0;
    int failed = 0;
    for (const golden::Case& item : manifest.cases) {
        if (!matchesFilter(item, options)) continue;
        for (const int page : item.pages) {
            for (const int dpi : item.dpi) {
                const golden::RenderOutcome outcome =
                    golden::renderPage(options.corpusRoot, item, page, dpi);
                if (!outcome.ok) {
                    err() << "golden-update: " << outcome.error << "\n";
                    ++failed;
                    continue;
                }

                const QString path = QDir(options.goldenRoot)
                                         .filePath(golden::baselineRelativePath(item, page, dpi));
                QString error;
                if (!golden::writePng(path, outcome.image, &error)) {
                    err() << "golden-update: " << error << "\n";
                    ++failed;
                    continue;
                }

                golden::CaptureRecord record;
                record.file = item.file;
                record.page = page;
                record.dpi = dpi;
                record.width = outcome.image.width;
                record.height = outcome.image.height;
                record.substitutions = golden::describeSubstitutions(outcome.substitutions);

                auto existing = std::find_if(log.records.begin(), log.records.end(),
                                             [&](const golden::CaptureRecord& row) {
                                                 return row.file == item.file && row.page == page
                                                        && row.dpi == dpi;
                                             });
                if (existing == log.records.end()) log.records.push_back(std::move(record));
                else *existing = std::move(record);

                ++written;
                out() << "  wrote " << golden::baselineRelativePath(item, page, dpi) << "\n";
                out().flush();
            }
        }
    }

    if (failed > 0) {
        err() << "golden-update: " << failed
              << " baseline(s) could not be produced; nothing was recorded\n";
        err().flush();
        return 1;
    }
    if (written == 0) {
        err() << "golden-update: no case matched";
        for (const QString& filter : options.filters) err() << " --filter " << filter;
        err() << "\n";
        err().flush();
        return 1;
    }

    std::sort(log.records.begin(), log.records.end(),
              [](const golden::CaptureRecord& a, const golden::CaptureRecord& b) {
                  if (a.file != b.file) return a.file < b.file;
                  if (a.page != b.page) return a.page < b.page;
                  return a.dpi < b.dpi;
              });

    QString error;
    if (!golden::writeCaptureLog(capturePath(options), log, &error)) {
        err() << "golden-update: " << error << "\n";
        err().flush();
        return 1;
    }
    if (!appendUpdateLog(options, stamp, written)) {
        err().flush();
        return 1;
    }

    out() << "\ngolden-update: " << written << " baseline(s) regenerated against PDFium "
          << pdf::render::version() << "\n\n"
          << "Paste this into the commit that carries them:\n\n"
          << "    Golden: regenerate " << written << " baseline(s)\n\n"
          << "    Reason: " << options.reason << "\n"
          << "    PDFium: " << pdf::render::version() << "\n";
    out().flush();
    return 0;
}

// S6 T8 (D6-6, guide A-64): the appearance cases -- pages our builder draws, from the spec in
// `tests/golden/AppearanceCases` -- written to `golden/appearance/<name>.png` at `kAppearanceDpi`, recorded in
// `captured.json` as `appearance/<name>` and in `UPDATES.md` with the reason. LOOK at each before keeping it:
// a baseline captured from a wrong builder is green for ever.
int regenerateAppearance(const Options& options)
{
    golden::EngineSession session;
    if (!session.isReady()) {
        err() << "golden-update: " << session.error() << "\n";
        err().flush();
        return 1;
    }
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        err() << "golden-update: no scratch directory\n";
        err().flush();
        return 1;
    }
    golden::CaptureLog log = golden::loadCaptureLog(capturePath(options));
    log.error.clear();
    const QString stamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    log.pdfiumVersion = pdf::render::version();
    log.capturedUtc = stamp;
    log.reason = options.reason;

    int written = 0;
    for (const golden::AppearanceCase& item : golden::appearanceCases()) {
        if (!options.filters.isEmpty()
            && std::none_of(options.filters.begin(), options.filters.end(),
                            [&item](const QString& filter) { return item.name.contains(filter, Qt::CaseInsensitive); }))
            continue;
        const std::string bytes = golden::appearancePdf(item.annotations, item.rotate, true, item.overlay);
        if (bytes.empty()) {
            err() << "golden-update: our builder refused case " << item.name << "; nothing was recorded\n";
            err().flush();
            return 1;
        }
        const QString name = item.name + QStringLiteral(".pdf");
        QFile file(QDir(scratch.path()).filePath(name));
        if (!file.open(QIODevice::WriteOnly)
            || file.write(bytes.data(), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size())) {
            err() << "golden-update: cannot write " << file.fileName() << "\n";
            err().flush();
            return 1;
        }
        file.close();
        golden::Case probe;
        probe.file = name;
        probe.pages = {0};
        probe.dpi = {golden::kAppearanceDpi};
        const golden::RenderOutcome outcome = golden::renderPage(scratch.path(), probe, 0, golden::kAppearanceDpi);
        if (!outcome.ok) {
            err() << "golden-update: " << outcome.error << "\n";
            err().flush();
            return 1;
        }
        const QString relative = golden::appearanceBaselinePath(item);
        QString error;
        if (!golden::writePng(QDir(options.goldenRoot).filePath(relative), outcome.image, &error)) {
            err() << "golden-update: " << error << "\n";
            err().flush();
            return 1;
        }
        golden::CaptureRecord record;
        record.file = QStringLiteral("appearance/") + item.name;
        record.page = 0;
        record.dpi = golden::kAppearanceDpi;
        record.width = outcome.image.width;
        record.height = outcome.image.height;
        record.substitutions = golden::describeSubstitutions(outcome.substitutions);
        auto existing = std::find_if(log.records.begin(), log.records.end(),
                                     [&record](const golden::CaptureRecord& row) { return row.file == record.file; });
        if (existing == log.records.end()) log.records.push_back(std::move(record));
        else *existing = std::move(record);
        ++written;
        out() << "  wrote " << relative << " -- " << item.note << "\n";
        out().flush();
    }
    if (written == 0) {
        err() << "golden-update: no appearance case matched\n";
        err().flush();
        return 1;
    }
    std::sort(log.records.begin(), log.records.end(), [](const golden::CaptureRecord& a, const golden::CaptureRecord& b) {
        if (a.file != b.file) return a.file < b.file;
        if (a.page != b.page) return a.page < b.page;
        return a.dpi < b.dpi;
    });
    QString error;
    if (!golden::writeCaptureLog(capturePath(options), log, &error)) {
        err() << "golden-update: " << error << "\n";
        err().flush();
        return 1;
    }
    Options logged = options;
    logged.filters = options.filters.isEmpty() ? QStringList{QStringLiteral("appearance/")} : options.filters;
    if (!appendUpdateLog(logged, stamp, written)) {
        err().flush();
        return 1;
    }
    out() << "\ngolden-update: " << written << " appearance baseline(s) written against PDFium " << pdf::render::version()
          << " -- LOOK at each before it is kept\n";
    out().flush();
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const Options options = parse(QCoreApplication::arguments());

    if (!options.usageError.isEmpty()) {
        err() << "golden-update: " << options.usageError << "\n\n";
        err().flush();
        printUsage();
        return 2;
    }
    if (options.help) {
        printUsage();
        return 0;
    }
    if (options.explain) return explain(options);
    if (options.checkManifest) return checkManifest(options);
    if (options.survey) return survey(options);

    // THE REFUSAL (D2-3, guide case A-50). Exit 2 rather than 1: this is a usage error, and CI's
    // own convention is that 2 means *you asked for something the tool will not do* while 1 means
    // *the thing you asked for failed*.
    if (options.reason.trimmed().isEmpty()) {
        err() << "golden-update: refusing to regenerate baselines without --reason.\n\n"
              << "  A golden baseline is a claim about ONE rasterizer (D2-3,\n"
              << "  claude-plan/Stage2-Rendering-Plan.md 3). Re-capturing one from the code that\n"
              << "  changed it turns a rendering regression into a new reference, and the diff\n"
              << "  then shows nothing but binary files.\n\n"
              << "  Say why, and it is recorded in golden/captured.json and golden/UPDATES.md:\n\n"
              << "      golden-update --reason \"PDFium 153.0.8009.0 -> 154.x: text AA changed\"\n";
        err().flush();
        return 2;
    }
    if (options.appearance) return regenerateAppearance(options);
    return regenerate(options);
}
