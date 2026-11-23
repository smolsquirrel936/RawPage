// tst_decorationgolden -- guide case A-98 (`Stage7-Assembly-Testing-Guide.md` §5.16), Stage 7 T14, decision D7-44: every
// decoration case (`tests/golden/DecorationCases`) written through the sink, rasterized, and compared with its baseline
// `golden/decoration/<name>.png` at the corpus suite's two thresholds (D2-13).
//
// WHAT A GREEN RUN CLAIMS -- the golden suite's bound, and no more (`golden/README.md`): our decorations rasterize on THIS
// rasterizer the way they did when each baseline was LOOKED AT and kept. Not that another reader draws them so (M-07,
// Acrobat), and not that they are right -- which is what looking at each first capture was for, and `golden/UPDATES.md`
// records that look. Both directions are held, as the appearance suite holds them: every case has a baseline, and
// every baseline under `golden/decoration/` is a case.

#include <catch2/catch_test_macros.hpp>

#include "tests/golden/DecorationCases.h"
#include "tests/golden/GoldenSuite.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>

using namespace rawpage;

namespace {

QString goldenRoot() { return QStringLiteral(RAWPAGE_GOLDEN_DIR); }

const golden::Manifest& manifest()
{
    static const golden::Manifest loaded = golden::loadManifest(QDir(goldenRoot()).filePath(QStringLiteral("manifest.json")));
    return loaded;
}

const golden::CaptureLog& captureLog()
{
    static const golden::CaptureLog loaded = golden::loadCaptureLog(QDir(goldenRoot()).filePath(QStringLiteral("captured.json")));
    return loaded;
}

} // namespace

TEST_CASE("decoration every case has a baseline and every baseline is a case", "[golden][decoration][A-98]")
{
    QSet<QString> named;
    for (const golden::DecorationCase& item : golden::decorationCases()) {
        INFO(item.name.toStdString());
        CHECK_FALSE(named.contains(item.name));
        named.insert(item.name);
        CHECK(QFileInfo::exists(QDir(goldenRoot()).filePath(golden::decorationBaselinePath(item))));
    }
    CHECK(named.size() >= 9);
    const QDir images(QDir(goldenRoot()).filePath(QStringLiteral("decoration")));
    for (const QString& file : images.entryList(QStringList{QStringLiteral("*.png")}, QDir::Files)) {
        INFO(file.toStdString());
        CHECK(named.contains(QFileInfo(file).completeBaseName()));
    }
}

namespace {

// Every case of one family -- headers, footers and Bates numbers, or (Stage 7 T15) watermarks -- compared.
void compareFamily(bool watermarks)
{
    REQUIRE(manifest().error.isEmpty());
    REQUIRE(captureLog().error.isEmpty());
    golden::EngineSession session;
    REQUIRE(session.isReady());
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    int compared = 0;
    int skipped = 0;
    int family = 0;
    for (const golden::DecorationCase& item : golden::decorationCases()) {
        if (item.watermark.has_value() != watermarks) continue;
        ++family;
        INFO(item.name.toStdString() << " -- " << item.note.toStdString());
        const std::string bytes = golden::decorationPdf(item);
        REQUIRE_FALSE(bytes.empty());   // a case the sink refused would be a page without its decoration
        const QString name = item.name + QStringLiteral(".pdf");
        QFile file(QDir(scratch.path()).filePath(name));
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size()));
        file.close();

        golden::Case probe;
        probe.file = name;
        probe.pages = {0};
        probe.dpi = {golden::kDecorationDpi};
        const golden::RenderOutcome outcome = golden::renderPage(scratch.path(), probe, 0, golden::kDecorationDpi);
        REQUIRE(outcome.ok);

        // A MACHINE-DEPENDENT case is compared only where this machine substitutes the faces the capture recorded --
        // the corpus suite's rule (`golden/README.md`, point 3), and anywhere else reported NOT APPLICABLE by name. Stage 7
        // guide I-35: CI's Windows runner drew `cjk`'s Han, kana and Hangul in faces of its own, 0.119 % of its pixels
        // past the threshold, and this case compared it anyway.
        if (item.machineDependent) {
            const golden::CaptureRecord* record = captureLog().find(QStringLiteral("decoration/") + item.name, 0, golden::kDecorationDpi);
            REQUIRE(record != nullptr);
            const QStringList here = golden::describeSubstitutions(outcome.substitutions);
            if (here != record->substitutions) {
                ++skipped;
                WARN("golden: not applicable -- decoration/" << item.name.toStdString() << ": this machine substitutes ["
                                                             << here.join(QLatin1String("; ")).toStdString()
                                                             << "], the baseline records ["
                                                             << record->substitutions.join(QLatin1String("; ")).toStdString() << "]");
                continue;
            }
        }

        core::RasterImage expected;
        QString error;
        REQUIRE(golden::readPng(QDir(goldenRoot()).filePath(golden::decorationBaselinePath(item)), &expected, &error));
        const golden::Comparison comparison = golden::compare(expected, outcome.image, manifest().thresholds);
        if (!comparison.passes(manifest().thresholds)) {
            const QString stem = QDir(QStringLiteral(RAWPAGE_GOLDEN_FAILURE_DIR)).filePath(QStringLiteral("decoration-") + item.name);
            (void)golden::writeFailureImages(stem, expected, outcome.image, manifest().thresholds, &error);
        }
        INFO(comparison.describe(manifest().thresholds).toStdString());
        CHECK(comparison.passes(manifest().thresholds));
        ++compared;
    }
    // Only a machine-dependent case may be skipped: every other one is compared, here and on any machine.
    int dependent = 0;
    for (const golden::DecorationCase& item : golden::decorationCases())
        dependent += (item.machineDependent && item.watermark.has_value() == watermarks) ? 1 : 0;
    CHECK(skipped <= dependent);
    CHECK(compared + skipped == family);
    CHECK(compared >= family - dependent);
    CHECK(family >= (watermarks ? 8 : 9));
}

} // namespace

TEST_CASE("decoration every case matches its baseline", "[golden][decoration][A-98]")
{
    compareFamily(false);
}

// Stage 7 T15 (D7-45): text, image and page sources, opacity, rotation, absolute and relative scale, anchor and offset,
// tiling, behind and in front, and a turned page -- each baseline LOOKED AT before it was kept (`golden/UPDATES.md`).
TEST_CASE("watermark every case matches its baseline", "[golden][decoration][watermark][A-99]")
{
    compareFamily(true);
}
