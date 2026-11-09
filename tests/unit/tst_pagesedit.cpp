// tst_pagesedit -- Stage 7 T8's headless halves (`Stage7-Assembly-Plan.md` D7-13, §6; `Stage7-Assembly-Testing-Guide.md`
// §5.10): rotation over a page set and its filters (A-59), Stage 3's journal line still replaying, the clipboard's
// form of a copied graph (A-55's codec), what each page carries (A-57's data), the snapshot's copy that a paste and a
// drop insert, and `documents/defaultPageSize` (A-45).
//
// WHAT IT DOES NOT COVER (convention 19): the pane, the keys and the dialogs -- `tests/ui/tst_thumbnailsedit.cpp`; the
// pages SHOWING their identities after a paste, a drag and a cut -- `tests/composition/tst_pagesproduct.cpp`, through
// the engine. The badges here are the STORE's reading, graded against the labels; that the pane paints them is the
// ui case's.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/cmd/CommandRegistry.h"
#include "core/doc/Assembly.h"
#include "core/doc/CopiedPages.h"
#include "core/doc/PageRanges.h"
#include "core/doc/PaperSize.h"
#include "core/settings/Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <set>
#include <vector>

using namespace rawpage;

namespace {

int rotationOf(const core::CommandJournal& journal, int page)
{
    return static_cast<int>(journal.document().pages().rotation(page));
}

std::vector<int> rotations(const core::CommandJournal& journal)
{
    std::vector<int> out;
    for (int page = 0; page < journal.document().pages().count(); ++page) out.push_back(rotationOf(journal, page));
    return out;
}

// A sink that refuses the `refuseAt`-th rotation it is asked for -- a refusal PART-WAY through a set.
class RefusingSink final : public core::MutationSink {
public:
    explicit RefusingSink(int refuseAt) : refuseAt_(refuseAt) {}
    core::Status setPageRotation(int page, int degrees) override
    {
        if (++asked_ == refuseAt_) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.test.refused"), QString()};
        held[page] = degrees;
        return {};
    }
    std::map<int, int> held;

    // Nothing else is asked of it here: each refuses, so a stray call shows up as a refusal.
    core::Status insertAnnotations(const std::vector<core::AnnotationPlacement>&, const std::vector<core::Annotation>&) override { return no(); }
    core::Status removeAnnotations(const std::vector<core::AnnotationPlacement>&) override { return no(); }
    core::Status replaceAnnotations(const std::vector<core::Annotation>&, const std::vector<core::Annotation>&) override { return no(); }
    core::Status insertAttachments(const std::vector<core::DocumentAttachment>&) override { return no(); }
    core::Status removeAttachments(const std::vector<QString>&) override { return no(); }
    core::Result<core::PageCommandReport> reorderPages(const core::PageMap&) override { return no().error(); }
    core::Result<core::PageCommandReport> removePages(const std::vector<int>&, const QString&) override { return no().error(); }
    core::Status restorePages(const std::vector<int>&, const QString&) override { return no(); }
    core::Result<core::DuplicatedPages> duplicatePages(const std::vector<int>&, const QString&,
                                                       const std::vector<std::vector<core::AnnotationCopy>>&) override
    {
        return no().error();
    }
    core::Result<core::InsertedPages> insertPages(int, const QString&, const QByteArray&) override { return no().error(); }
    core::Result<core::InsertedPages> replacePages(const std::vector<int>&, const QString&, const QByteArray&) override
    {
        return no().error();
    }
    core::Status restoreReplacedPages(const std::vector<int>&, const QString&) override { return no(); }
    core::Status setPageBoxes(const std::vector<int>&, const std::vector<core::PageBoxes>&, const QString&) override { return no(); }
    core::Status restorePageBoxes(const std::vector<int>&, const QString&) override { return no(); }
    core::Status wrapPageContents(const std::vector<int>&, const std::vector<core::PageContentWrap>&, const QString&) override { return no(); }
    core::Status unwrapPageContents(const std::vector<int>&, const QString&) override { return no(); }
    core::Result<std::vector<QString>> setPageLabels(const std::vector<int>&, const std::vector<core::PageLabelSpec>&, const QString&) override
    {
        return no().error();
    }
    core::Result<std::vector<QString>> restorePageLabels(const QString&) override { return no().error(); }
    core::Status setPageThumbnails(const std::vector<int>&, const std::vector<core::PageThumbnail>&, const QString&) override { return no(); }
    core::Status restorePageThumbnails(const std::vector<int>&, const QString&) override { return no(); }
    core::Status setOutline(const core::Outline&) override { return no(); }

private:
    static core::Status no() { return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.test.unexpected"), QString()}; }
    int refuseAt_ = 0;
    int asked_ = 0;
};

} // namespace

// =============================================================================================
// A-59 -- rotation over a set, a range, odd, even, one orientation; Stage 3's line still replays
// =============================================================================================

TEST_CASE("rotate.ranges each filter keeps exactly its pages", "[pagesedit][rotate.ranges][A-59]")
{
    // `mixed-page-sizes.pdf`: pages of several sizes, some landscape -- the orientation each page SHOWS is read back from
    // the tree, and graded against the one computation the filter does not share: width against height, here.
    auto session = tests::openCopy("ordinary/mixed-page-sizes.pdf");
    REQUIRE(session != nullptr);
    const core::PageTree& tree = session->journal->document().pages();
    const int count = tree.count();
    REQUIRE(count >= 4);
    std::vector<int> every;
    std::vector<int> landscape;
    std::vector<int> portrait;
    for (int page = 0; page < count; ++page) {
        every.push_back(page);
        const core::PdfSize shown = tree.displaySize(page);
        (shown.width > shown.height ? landscape : portrait).push_back(page);
    }
    REQUIRE_FALSE(landscape.empty());   // the fixture has both, or the orientation half proves nothing
    REQUIRE_FALSE(portrait.empty());

    using core::PageOrientation;
    using core::PageParity;
    CHECK(core::filterPages(every, PageParity::Any, PageOrientation::Any, tree) == every);
    std::vector<int> odd;
    std::vector<int> even;
    for (const int page : every) (page % 2 == 0 ? odd : even).push_back(page);   // page NUMBER page + 1
    CHECK(core::filterPages(every, PageParity::Odd, PageOrientation::Any, tree) == odd);
    CHECK(core::filterPages(every, PageParity::Even, PageOrientation::Any, tree) == even);
    CHECK(core::filterPages(every, PageParity::Any, PageOrientation::Landscape, tree) == landscape);
    CHECK(core::filterPages(every, PageParity::Any, PageOrientation::Portrait, tree) == portrait);
    std::vector<int> oddLandscape;
    for (const int page : landscape)
        if (page % 2 == 0) oddLandscape.push_back(page);
    CHECK(core::filterPages(every, PageParity::Odd, PageOrientation::Landscape, tree) == oddLandscape);
    // A page outside the document is dropped, never kept.
    CHECK(core::filterPages({0, count, count + 5}, PageParity::Any, PageOrientation::Any, tree) == std::vector<int>{0});

    // ORIENTATION IS AS SHOWN: a quarter turn of a portrait page makes it landscape to the filter.
    const int turned = portrait.front();
    REQUIRE(session->rotate(turned, 90));
    const std::vector<int> nowLandscape = core::filterPages(every, PageParity::Any, PageOrientation::Landscape, tree);
    CHECK(std::find(nowLandscape.begin(), nowLandscape.end(), turned) != nowLandscape.end());
}

TEST_CASE("rotate.ranges a set is one command and one undo step and turns exactly its pages", "[pagesedit][rotate.ranges][A-59]")
{
    auto session = tests::openCopy("assembly/odd-count-9.pdf");
    REQUIRE(session != nullptr);
    const std::vector<int> before = rotations(*session->journal);
    REQUIRE(session->journal->run(std::make_unique<core::RotatePageCommand>(std::vector<int>{1, 4, 8}, 270)));
    CHECK(session->journal->historySize() == 1);
    CHECK(session->journal->undoStep().size() == 1);
    for (int page = 0; page < 9; ++page) {
        INFO("page " << page);
        const bool named = page == 1 || page == 4 || page == 8;
        CHECK(rotationOf(*session->journal, page) == (named ? (before[static_cast<std::size_t>(page)] + 270) % 360
                                                            : before[static_cast<std::size_t>(page)]));
    }
    REQUIRE(session->journal->undo());
    CHECK(rotations(*session->journal) == before);

    // THE SAVED FILE turns exactly those pages too -- the sink's half, read back from the bytes.
    REQUIRE(session->journal->redo());
    auto saved = session->coordinator->save(*session->journal, session->path);
    REQUIRE(saved);
    tests::CosBackend fresh;
    auto handle = fresh.open(session->path, nullptr);
    REQUIRE(handle);
    const core::PageTree* written = fresh.pages(handle.value());
    REQUIRE(written != nullptr);
    for (int page = 0; page < 9; ++page) {
        INFO("saved page " << page);
        CHECK(static_cast<int>(written->rotation(page)) == rotationOf(*session->journal, page));
    }
}

TEST_CASE("rotate.ranges a sink refusing part-way leaves every page as it was", "[pagesedit][rotate.ranges][A-59]")
{
    // CONTROL FOR THE ROLLBACK: the third of four pages is refused, so the first two were turned at the sink -- and must
    // be turned back, with the model never touched (`Command.h`'s contract for a `false` return).
    auto session = tests::openCopy("assembly/odd-count-5.pdf");
    REQUIRE(session != nullptr);
    core::Document document;
    REQUIRE(document.hydrate(session->backend, session->handle));
    const std::vector<int> before = [&] {
        std::vector<int> out;
        for (int page = 0; page < document.pages().count(); ++page) out.push_back(static_cast<int>(document.pages().rotation(page)));
        return out;
    }();
    RefusingSink sink(3);
    core::RotatePageCommand command(std::vector<int>{0, 1, 2, 3}, 90);
    CHECK_FALSE(command.apply(document, sink));
    for (int page = 0; page < 4; ++page) {
        INFO("page " << page);
        CHECK(static_cast<int>(document.pages().rotation(page)) == before[static_cast<std::size_t>(page)]);
        if (page < 2) CHECK(sink.held.at(page) == before[static_cast<std::size_t>(page)]);   // turned, then turned back
    }
    CHECK(sink.held.count(2) == 0);
}

TEST_CASE("rotate.ranges a journal line Stage 3 wrote still replays and one page still writes it", "[pagesedit][rotate.ranges][A-59]")
{
    // STAGE 3'S LINE, as `RecoveryJournal` wrote it from Stage 3 T9 to Stage 7 T7 -- read as a one-page command, and a
    // one-page command written back as exactly that line (the format GREW; an old line is still a line).
    const QByteArray stage3 = R"({"degrees":90,"page":2,"type":"page.rotate"})";
    const QJsonObject json = QJsonDocument::fromJson(stage3).object();
    auto command = core::CommandRegistry::deserialize(json);
    REQUIRE(command);
    const auto* rotate = dynamic_cast<const core::RotatePageCommand*>(command.value().get());
    REQUIRE(rotate != nullptr);
    CHECK(rotate->pages() == std::vector<int>{2});
    CHECK(rotate->degrees() == 90);
    CHECK(QJsonDocument(rotate->serialize()).toJson(QJsonDocument::Compact) == stage3);
    CHECK(rotate->name().key == QLatin1String("command.page.rotate"));

    // ... and it APPLIES: the replay of an old journal turns the page it named.
    auto session = tests::openCopy("assembly/odd-count-5.pdf");
    REQUIRE(session != nullptr);
    const int was = rotationOf(*session->journal, 2);
    REQUIRE(session->journal->run(std::move(command).value()));
    CHECK(rotationOf(*session->journal, 2) == (was + 90) % 360);

    // A set's line names its pages and reads back as the same set.
    const core::RotatePageCommand set(std::vector<int>{0, 3}, 180);
    const QJsonObject written = set.serialize();
    CHECK(written.value(QStringLiteral("pages")).toArray() == QJsonArray{0, 3});
    CHECK_FALSE(written.contains(QStringLiteral("page")));
    CHECK(set.name().key == QLatin1String("command.page.rotatePages"));
}

// =============================================================================================
// A-55's codec -- pages on the clipboard
// =============================================================================================

TEST_CASE("thumbnails.clipboard the payload round-trips and someone else's bytes are checked", "[pagesedit][clipboard][A-55]")
{
    core::CopiedPages pages;
    pages.graph = QByteArray("%PDF-1.7\n") + QByteArray(70000, 'x');   // past a journal line's 64 KiB, as a real one is
    pages.pageCount = 3;
    const QByteArray encoded = core::encodeCopiedPages(pages);
    auto decoded = core::decodeCopiedPages(encoded);
    REQUIRE(decoded);
    CHECK(decoded.value().graph == pages.graph);
    CHECK(decoded.value().pageCount == 3);
    CHECK(decoded.value().report.isEmpty());   // what the copy met is not carried: a paste reports what the sink meets

    const auto refused = [](const QByteArray& bytes) {
        auto result = core::decodeCopiedPages(bytes);
        return !result && result.error().messageKey == QLatin1String("error.clipboard.malformed");
    };
    const auto with = [&](const QJsonObject& json) { return QJsonDocument(json).toJson(QJsonDocument::Compact); };
    const QString graph = QString::fromLatin1(QByteArray("%PDF-1.7 x").toBase64());
    CHECK(refused(QByteArray("not json")));
    CHECK(refused(with({{"version", 2}, {"pages", 1}, {"graph", graph}})));
    CHECK(refused(with({{"pages", 1}, {"graph", graph}})));
    CHECK(refused(with({{"version", 1}, {"pages", 0}, {"graph", graph}})));
    CHECK(refused(with({{"version", 1}, {"pages", 1.5}, {"graph", graph}})));
    CHECK(refused(with({{"version", 1}, {"pages", 2000000}, {"graph", graph}})));
    CHECK(refused(with({{"version", 1}, {"pages", "1"}, {"graph", graph}})));
    CHECK(refused(with({{"version", 1}, {"pages", 1}})));
    CHECK(refused(with({{"version", 1}, {"pages", 1}, {"graph", QStringLiteral("!!not base64!!")}})));
    CHECK(refused(with({{"version", 1}, {"pages", 1}, {"graph", QString::fromLatin1(QByteArray("GIF89a").toBase64())}})));
    // THE BOUND before the parser: a payload larger than the largest graph could encode to is refused unread.
    CHECK(refused(QByteArray((core::kMaxCopiedGraphBytes / 3 + 1) * 4 + 2048, ' ')));
    // The control: the same shape with every field right is read.
    CHECK(core::decodeCopiedPages(with({{"version", 1}, {"pages", 1}, {"graph", graph}})));
}

TEST_CASE("thumbnails.clipboard the snapshot's copy is the pages as they stand and inserts as a payload", "[pagesedit][clipboard][A-55]")
{
    // `AssemblySource::copy` -- what `page.copy` and a drop into another window take -- over a document with an UNSAVED
    // rotation: the copy carries it, because what the screen shows is what is copied (D7-30(a)'s rule).
    auto source = tests::openCopy("assembly/refs-every-kind.pdf");
    REQUIRE(source != nullptr);
    REQUIRE(source->rotate(3, 90));
    auto snapshot = source->backend.assemblySource(source->handle);
    REQUIRE(snapshot);
    auto copied = snapshot.value()->copy({3, 5});
    REQUIRE(copied);
    CHECK(copied.value().pageCount == 2);
    CHECK(copied.value().graph.startsWith("%PDF-"));
    CHECK(source->journal->document().pages().count() == 12);   // the source changes in nothing

    // Through the clipboard's form and into ANOTHER document, as a paste does: two pages, the first turned 90.
    auto decoded = core::decodeCopiedPages(core::encodeCopiedPages(copied.value()));
    REQUIRE(decoded);
    auto target = tests::openCopy("assembly/odd-count-3.pdf");
    REQUIRE(target != nullptr);
    REQUIRE(target->journal->run(std::make_unique<core::InsertPagesCommand>(1, std::move(decoded).value())));
    CHECK(target->journal->document().pages().count() == 5);
    CHECK(static_cast<int>(target->journal->document().pages().rotation(1))
          == (static_cast<int>(source->journal->document().pages().rotation(3))));
    CHECK(static_cast<int>(target->journal->document().pages().rotation(1)) % 360 != 0);

    // A page outside the snapshot is refused by the copier's key, nothing made.
    CHECK_FALSE(snapshot.value()->copy({12}));
}

// =============================================================================================
// A-57's data -- what each page carries, against the labels
// =============================================================================================

TEST_CASE("thumbnails.badges the store says what each page carries as the labels name it", "[pagesedit][badges][A-57]")
{
    using Carries = core::AnnotationStore::PageCarries;
    {
        // EVERY PAGE of `annots-on-pages.pdf` carries a note with its pop-up, a reply, a link and a widget (its labels).
        auto session = tests::openCopy("assembly/annots-on-pages.pdf");
        REQUIRE(session != nullptr);
        const core::AnnotationStore& store = session->journal->document().annotations();
        REQUIRE(store.pageCount() > 0);
        for (int page = 0; page < store.pageCount(); ++page) {
            INFO("annots-on-pages page " << page);
            CHECK(store.carries(page) == Carries{true, true, true});
        }
    }
    {
        // `refs-every-kind.pdf`: LINKS on pages 1 to 4 and nothing else (its labels' `links`) -- the control that the
        // badge is per page, not per document.
        auto session = tests::openCopy("assembly/refs-every-kind.pdf");
        REQUIRE(session != nullptr);
        const core::AnnotationStore& store = session->journal->document().annotations();
        const std::set<int> linked{0, 1, 2, 3};
        for (int page = 0; page < 12; ++page) {
            INFO("refs-every-kind page " << page);
            CHECK(store.carries(page) == Carries{false, false, linked.count(page) != 0});
        }
    }
    {
        // `forms-acroform.pdf`: one page of widgets and nothing else (`corpus/PROVENANCE.md`'s row).
        auto session = tests::openCopy("ordinary/forms-acroform.pdf");
        REQUIRE(session != nullptr);
        CHECK(session->journal->document().annotations().carries(0) == Carries{false, true, false});
    }
}

// =============================================================================================
// A-45's key -- documents/defaultPageSize
// =============================================================================================

TEST_CASE("pageops.blank documents/defaultPageSize is A4 by default and round-trips every size", "[pagesedit][settings][A-45]")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.ini"));
    {
        core::Settings settings(path);
        CHECK(settings.get(core::keys::documents::defaultPageSize) == core::PaperSize::A4);
        CHECK(core::paperSizePoints(core::PaperSize::A4) == core::PdfSize{595.0, 842.0});
        CHECK(core::paperSizePoints(core::PaperSize::Letter) == core::PdfSize{612.0, 792.0});
        settings.set(core::keys::documents::defaultPageSize, core::PaperSize::Legal);
    }
    core::Settings reread(path);
    CHECK(reread.get(core::keys::documents::defaultPageSize) == core::PaperSize::Legal);
    for (const core::PaperSize size : core::kPaperSizes) CHECK(core::paperSizeFromName(core::paperSizeName(size)) == size);
    CHECK_FALSE(core::paperSizeFromName("tabloid").has_value());
}
