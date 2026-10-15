// tst_migratecorpus -- comments migrated from `annots-report-v1.pdf` to `compare/report-v2.pdf`, graded on the guide's
// §3.5 against `corpus/labels/annots-migration.json`. Stage 6 T13, decision D6-21 (`Stage6-Annotations-Plan.md` §3),
// guide cases A-98 and A-99 (`Stage6-Annotations-Testing-Guide.md` §5.16).
//
// THE INPUTS ARE THE ENGINE'S: every page of both documents extracted by the shipped backend's `ITextExtractor` and
// laid out by `PageReading`, the pages aligned by Stage 5's `DocumentComparer` at *text only* -- what the product
// hands the migrator. THE ORACLE IS THE GENERATOR'S: `corpus/make-annots.py` placed every comment on text it knows,
// and wrote where each must land in the revision (from `make-compare.py`'s own lines, never from an extraction), or
// that it must be reported.
//
// §3.5's bars: PRECISION 100 % (no comment placed on a page or text but the label's), RECALL at least 95 % of the
// placeable, EVERY unplaceable one in the report and none of it placed, and POSITION -- markup's quadrilaterals within
// 1 point of the label's, any other comment's rectangle within 2.
//
// WHAT THIS DOES NOT COVER (convention 19): the product's gathering of the text through `DocumentIndex` and the run
// on its own thread, the merge into the revision's journal, and the report a person reads --
// `tests/composition/tst_exchangeproduct` and M-10. Both documents are ours, in base-14 Helvetica.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/AnnotationStore.h"
#include "core/doc/CommentMigrator.h"
#include "core/text/DocumentComparer.h"
#include "core/text/TextSelection.h"
#include "tests/render/TextOracle.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using namespace rawpage::test_oracle;
using core::MigrationEntry;
using core::text::PageReading;

namespace {

struct Side {
    std::unique_ptr<Opened> opened;
    core::text::CompareDocument pages;
    core::CommentMigrator::Pages readings;
};

Side load(const std::string& relative)
{
    Side side;
    side.opened = openCorpus(relative);
    for (int i = 0; i < side.opened->pageCount(); ++i) {
        core::text::ComparePage page;
        page.reading = PageReading::build(std::make_shared<const PageTextModel>(extract(*side.opened, i)));
        page.cropBox = side.opened->page(i).cropBox();
        page.rotation = side.opened->page(i).rotation();
        side.readings.push_back(page.reading);
        side.pages.push_back(std::move(page));
    }
    return side;
}

bool within(const std::vector<double>& got, const QJsonArray& want, double tolerance)
{
    if (static_cast<qsizetype>(got.size()) != want.size()) return false;
    for (qsizetype i = 0; i < want.size(); ++i) {
        if (std::fabs(got[static_cast<std::size_t>(i)] - want[i].toDouble()) > tolerance) return false;
    }
    return true;
}

} // namespace

TEST_CASE("comments migrate to the revision on the guide's bars and what cannot is reported and not placed",
          "[migrate][corpus][A-98][A-99][D6-21]")
{
    Side base = load("ordinary/annots-report-v1.pdf");
    Side revised = load("compare/report-v2.pdf");
    const QJsonObject label = loadLabel("annots-migration");

    // STAGE 5'S ALIGNMENT: the comparer's, at text only -- and it is the label's page map, or nothing below means
    // anything.
    const core::text::ComparisonResult compared =
        core::text::DocumentComparer::compare(base.pages, revised.pages, core::text::CompareOptions{});
    const QJsonObject pageMap = label.value(QStringLiteral("pageMap")).toObject();
    for (auto it = pageMap.begin(); it != pageMap.end(); ++it) {
        const int from = it.key().toInt();
        const int to = it.value().toInt();
        bool aligned = false;
        for (const core::text::PageMatch& m : compared.pages) aligned = aligned || (m.base == from && m.revised == to);
        INFO("page " << from << " -> " << to);
        REQUIRE(aligned);
    }

    const core::AnnotationStore* store = base.opened->backend.annotations(base.opened->handle);
    REQUIRE(store != nullptr);
    const core::Fdf original = store->exportFdf(store->topLevel());
    const core::MigrationResult result = core::CommentMigrator::migrate(original, base.readings, revised.readings, compared.pages);

    std::map<QString, const MigrationEntry*> entryOf;
    for (const MigrationEntry& e : result.entries) entryOf.emplace(e.name, &e);
    std::map<QString, const core::Annotation*> placedOf;
    for (const core::Annotation& a : result.placed.annotations) placedOf.emplace(a.id.nm, &a);

    int placeable = 0;
    int placedRight = 0;
    int placedWrong = 0;
    int unplaceable = 0;
    int reportedRight = 0;
    for (const QJsonValue value : label.value(QStringLiteral("annotations")).toArray()) {
        const QJsonObject want = value.toObject();
        const QString name = want.value(QStringLiteral("nm")).toString();
        INFO(name.toStdString() << " " << want.value(QStringLiteral("subtype")).toString().toStdString() << ": "
                                << want.value(QStringLiteral("why")).toString().toStdString());
        const auto entry = entryOf.find(name);
        REQUIRE(entry != entryOf.end());
        const auto placed = placedOf.find(name);
        if (want.value(QStringLiteral("placeable")).toBool()) {
            ++placeable;
            if (entry->second->outcome != MigrationEntry::Outcome::Placed) {
                UNSCOPED_INFO("NOT PLACED: " << name.toStdString() << " -- "
                                             << (entry->second->why ? std::string(entry->second->why->messageKey.data()) : std::string()));
                continue;
            }
            REQUIRE(placed != placedOf.end());
            const core::Annotation& a = *placed->second;
            const bool page = a.page == want.value(QStringLiteral("expectedPage")).toInt();
            bool where = false;
            if (want.contains(QStringLiteral("expectedQuadPoints"))) {
                where = within(a.quadPoints, want.value(QStringLiteral("expectedQuadPoints")).toArray(), 1.0);
            } else {
                where = within({a.rect.left(), a.rect.bottom(), a.rect.right(), a.rect.top()},
                               want.value(QStringLiteral("expectedRect")).toArray(), 2.0);
            }
            CHECK(page);
            CHECK(where);
            if (page && where) ++placedRight;
            else ++placedWrong;
        } else {
            ++unplaceable;
            // A-99: IN THE REPORT, AND NOTHING OF IT PLACED.
            CHECK(entry->second->outcome == MigrationEntry::Outcome::Reported);
            CHECK(entry->second->why.has_value());
            CHECK(placed == placedOf.end());
            if (entry->second->outcome == MigrationEntry::Outcome::Reported && placed == placedOf.end()) ++reportedRight;
            if (placed != placedOf.end()) ++placedWrong;
        }
    }
    const int placedTotal = result.count(MigrationEntry::Outcome::Placed);
    const double precision = placedTotal == 0 ? 0.0 : 100.0 * placedRight / placedTotal;
    const double recall = placeable == 0 ? 0.0 : 100.0 * placedRight / placeable;
    std::printf("A-98: %d placed, %d of them where the label says (precision %.1f %%); %d of %d placeable placed (recall %.1f %%); "
                "A-99: %d of %d unplaceable reported and not placed\n",
                placedTotal, placedRight, precision, placedRight, placeable, recall, reportedRight, unplaceable);
    CHECK(placedWrong == 0);
    CHECK(precision == 100.0);
    CHECK(recall >= 95.0);
    CHECK(reportedRight == unplaceable);
    CHECK(unplaceable > 0);

    // THE MERGE INTO THE REVISION adds every placed comment: the revision holds none of them yet.
    const core::AnnotationStore* target = revised.opened->backend.annotations(revised.opened->handle);
    REQUIRE(target != nullptr);
    const core::FdfImport plan = target->importFdf(result.placed, [&revised](const core::Annotation& a) {
        return revised.opened->backend.annotationWritable(a);
    });
    CHECK(plan.result.count(core::MergeEntry::Outcome::Added) == static_cast<int>(result.placed.annotations.size()) -
                                                                     static_cast<int>(std::count_if(result.placed.annotations.begin(), result.placed.annotations.end(),
                                                                                                    [](const core::Annotation& a) { return a.subtype == core::AnnotationSubtype::Popup; })));
    CHECK(plan.result.count(core::MergeEntry::Outcome::Refused) == 0);
}
