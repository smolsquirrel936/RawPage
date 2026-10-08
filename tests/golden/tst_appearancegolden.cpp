// tst_appearancegolden -- guide case A-64 (`Stage6-Annotations-Testing-Guide.md` §5.11), Stage 6 T8, decision D6-6:
// every appearance case (`tests/golden/AppearanceCases`) rasterized from the page our builder writes, compared with
// its baseline `golden/appearance/<name>.png` at the corpus suite's two thresholds (D2-13).
//
// WHAT A GREEN RUN CLAIMS -- the golden suite's bound, and no more (`golden/README.md`): our builder's streams
// rasterize on THIS rasterizer the way they did when each baseline was LOOKED AT and kept. Not that another reader
// draws them so (M-02, Acrobat and Firefox), and not that they are right -- which is what looking at each first
// capture was for, and `golden/UPDATES.md` records that look.
//
// Both directions are held, as `--check-manifest` holds the corpus: every case has a baseline, and every baseline
// under `golden/appearance/` is a case -- a case deleted without its image is a reference nobody compares.

#include <catch2/catch_test_macros.hpp>

#include "tests/golden/AppearanceCases.h"
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
    static const golden::Manifest loaded =
        golden::loadManifest(QDir(goldenRoot()).filePath(QStringLiteral("manifest.json")));
    return loaded;
}

} // namespace

TEST_CASE("appearance every case has a baseline and every baseline is a case", "[golden][appearance][A-64]")
{
    QSet<QString> named;
    for (const golden::AppearanceCase& item : golden::appearanceCases()) {
        INFO(item.name.toStdString());
        CHECK_FALSE(named.contains(item.name));   // a name once
        named.insert(item.name);
        CHECK(QFileInfo::exists(QDir(goldenRoot()).filePath(golden::appearanceBaselinePath(item))));
    }
    CHECK(named.size() >= 30);
    const QDir images(QDir(goldenRoot()).filePath(QStringLiteral("appearance")));
    for (const QString& file : images.entryList(QStringList{QStringLiteral("*.png")}, QDir::Files)) {
        INFO(file.toStdString());
        CHECK(named.contains(QFileInfo(file).completeBaseName()));
    }
}

TEST_CASE("appearance every case matches its baseline", "[golden][appearance][A-64]")
{
    REQUIRE(manifest().error.isEmpty());
    golden::EngineSession session;
    REQUIRE(session.isReady());
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    int compared = 0;
    for (const golden::AppearanceCase& item : golden::appearanceCases()) {
        INFO(item.name.toStdString() << " -- " << item.note.toStdString());
        const std::string bytes = golden::appearancePdf(item.annotations, item.rotate, true);
        REQUIRE_FALSE(bytes.empty());   // a case our builder refused would be a page without it
        const QString name = item.name + QStringLiteral(".pdf");
        QFile file(QDir(scratch.path()).filePath(name));
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size()));
        file.close();

        golden::Case probe;
        probe.file = name;
        probe.pages = {0};
        probe.dpi = {golden::kAppearanceDpi};
        const golden::RenderOutcome outcome = golden::renderPage(scratch.path(), probe, 0, golden::kAppearanceDpi);
        REQUIRE(outcome.ok);

        core::RasterImage expected;
        QString error;
        REQUIRE(golden::readPng(QDir(goldenRoot()).filePath(golden::appearanceBaselinePath(item)), &expected, &error));
        const golden::Comparison comparison = golden::compare(expected, outcome.image, manifest().thresholds);
        if (!comparison.passes(manifest().thresholds)) {
            const QString stem = QDir(QStringLiteral(RAWPAGE_GOLDEN_FAILURE_DIR)).filePath(QStringLiteral("appearance-") + item.name);
            (void)golden::writeFailureImages(stem, expected, outcome.image, manifest().thresholds, &error);
        }
        INFO(comparison.describe(manifest().thresholds).toStdString());
        CHECK(comparison.passes(manifest().thresholds));
        ++compared;
    }
    CHECK(compared == static_cast<int>(golden::appearanceCases().size()));
}
