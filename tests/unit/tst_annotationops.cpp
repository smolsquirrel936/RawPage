// tst_annotationops -- the annotation write path, one type end to end. Stage 6 T3, guide cases A-24 …
// A-30 (`Stage6-Annotations-Testing-Guide.md` §5.5), decisions D6-1, D6-4, D6-5 and D6-6.
//
// WHAT THIS SUITE IS TRYING TO CATCH: an update that holds an object the edit did not need (A-24); an
// undo before a save that still writes, or an undo after one that cannot list the annotation again
// (A-25); an edit that drops a key nobody here models (A-26); a created annotation without its identity
// (A-27); a save whose verify passes over a file missing the annotation it was meant to add (A-30); a
// content stream another reader would refuse (D6-6); a refusal that leaves a trace.
//
// WHAT IT DOES NOT COVER (convention 19):
//   * the ENGINE. The backend is `tests/unit/CosBackend.h`; whether PDFium draws what was written, and
//     reads the saved file's annotations as our reader does, is `tests/render/tst_saveengine.cpp`'s
//     (A-33's case) and, for the pixels, T4's `tst_annotdisplay`;
//   * whether the appearance LOOKS right -- golden rasters, PDFium's own generation, Acrobat and Firefox
//     (D6-6's other oracles, T8 and M-02). `ContentStreamBuilder::check` proves well-formed, no more;
//   * `qpdf`: every saved file is copied to RAWPAGE_WRITTEN_DIR, and the portable-asan CI job runs
//     `qpdf --check` over them (A-31) -- this machine has no qpdf;
//   * A-26 through a COMMAND: `EditAnnotationCommand` is T5's, so the preservation rule is held here at
//     `AnnotationOps::editedDictionary`, the function that command will call.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "TinyPdf.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "pdf/cos/ContentStreamBuilder.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AnnotationOps.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
#include "pdf/ops/EmbeddedFileReader.h"
#include "pdf/ops/RevisionWriter.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using core::AddAnnotationCommand;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;
using core::DeleteAnnotationCommand;
using pdf::cos::ContentStreamBuilder;
using pdf::cos::CosObject;

namespace {

constexpr std::int64_t kNow = 1791504000;   // 2026-10-09T00:00:00Z, a fixed instant
const QString kAuthor = QStringLiteral("Ana Tester");

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

std::string_view viewOf(const QByteArray& bytes)
{
    return std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

// A highlight over the box (x, y, w, h) of `page`, its quadrilateral in ACROBAT's corner order --
// top-left, top-right, bottom-left, bottom-right.
Annotation highlight(int page, double x = 72, double y = 700, double w = 200, double h = 14)
{
    Annotation a = core::newAnnotation(AnnotationSubtype::Highlight, page, kAuthor, kNow);
    a.quadPoints = {x, y + h, x + w, y + h, x, y, x + w, y};
    a.rect = *core::boundsOfPoints(a.quadPoints);
    a.color = {1.0, 1.0, 0.0};
    a.opacity = 0.5;
    a.contents = QStringLiteral("Highlighted by a test");
    return a;
}

core::Status add(tests::SaveSession& session, std::vector<Annotation> annotations)
{
    return session.journal->run(std::make_unique<AddAnnotationCommand>(std::move(annotations)));
}

core::Status remove(tests::SaveSession& session, std::vector<AnnotationId> ids)
{
    return session.journal->run(std::make_unique<DeleteAnnotationCommand>(std::move(ids)));
}

// Every file a case saves is kept for `qpdf --check` (A-31), under a name that says which case wrote it.
void keepForQpdf(const QString& path, const QString& name)
{
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);
    REQUIRE(QDir().mkpath(dir));
    const QString target = QDir(dir).filePath(name);
    QFile::remove(target);
    REQUIRE(QFile::copy(path, target));
}

QByteArray save(tests::SaveSession& session, const QString& keepAs)
{
    auto result = session.coordinator->save(*session.journal, session.path);
    INFO("save error: " << (result ? std::string() : keyOf(result.error()) + " " + result.error().context.toStdString()));
    REQUIRE(result);
    REQUIRE(result.value().kind == core::SaveOutcome::Kind::Saved);
    if (!keepAs.isEmpty()) keepForQpdf(session.path, keepAs);
    return tests::readAllBytes(session.path);
}

// The objects the next save would append, ascending -- the composed update, never written.
std::vector<long long> objectsTheUpdateWrites(tests::SaveSession& session)
{
    auto composed = pdf::ops::RevisionWriter::compose(*session.sink(), false);
    REQUIRE(composed);
    return composed.value().objectsWritten;
}

// A file read back FROM ITS BYTES with the first-party reader: its table and its annotations.
struct ReadBack {
    QByteArray bytes;
    std::optional<pdf::cos::XrefTable> table;
    pdf::ops::PageWalk walk;
    core::AnnotationStore store;

    explicit ReadBack(QByteArray fileBytes) : bytes(std::move(fileBytes))
    {
        auto opened = pdf::cos::openWithRepair(viewOf(bytes));
        REQUIRE(opened);
        REQUIRE(opened.value().usedOriginalXref);
        table.emplace(std::move(opened.value().table));
        auto walked = pdf::ops::PageOps::walk(*table);
        REQUIRE(walked);
        walk = std::move(walked).value();
        store = core::AnnotationStore::fromReading(pdf::ops::AnnotationReader::read(*table, walk));
    }
    CosObject object(long long number)
    {
        auto fetched = table->fetch(number);
        REQUIRE(fetched);
        return fetched.value().object;
    }
};

std::vector<AnnotationId> idsOnPage(const core::AnnotationStore& store, int page)
{
    std::vector<AnnotationId> ids;
    for (const std::size_t index : store.onPage(page)) ids.push_back(store.at(index).id);
    return ids;
}

} // namespace

// ==========================================================================================
// D6-6 -- the content-stream builder, and the re-read every generated stream passes
// ==========================================================================================

TEST_CASE("content stream builder spells every number as PDF can read it", "[annotops][contentstream]")
{
    CHECK(ContentStreamBuilder::number(0.5) == "0.5");
    CHECK(ContentStreamBuilder::number(1.0) == "1");
    CHECK(ContentStreamBuilder::number(-2.25) == "-2.25");
    CHECK(ContentStreamBuilder::number(72.123456) == "72.1235");
    CHECK(ContentStreamBuilder::number(1e-5) == "0");        // never 1e-05
    CHECK(ContentStreamBuilder::number(-0.00001) == "0");    // never -0
    CHECK(ContentStreamBuilder::number(1e10) == "10000000000");
    CHECK(ContentStreamBuilder::number(std::nan("")) == "0");
    CHECK(ContentStreamBuilder::number(INFINITY) == "0");
    for (const double v : {1e-7, 123456789.987654, -1e9, 0.1 + 0.2}) {
        const std::string text = ContentStreamBuilder::number(v);
        INFO("value " << v << " spelled " << text);
        CHECK(text.find_first_of("eE,") == std::string::npos);
    }
}

TEST_CASE("content stream re-read accepts what the builder writes and refuses what no reader should get", "[annotops][contentstream]")
{
    ContentStreamBuilder built;
    const double rgb[3] = {1, 0.5, 0};
    const double gray[1] = {0.25};
    built.save().graphicsState("GS0").fillColor(rgb, 3).strokeColor(gray, 1).lineWidth(1.5)
        .moveTo(0, 0).lineTo(10, 0).curveTo(10, 5, 5, 10, 0, 10).closePath().fillAndStroke()
        .rectangle(1, 2, 3, 4).stroke().concat(1, 0, 0, 1, 5, 5).endPath().restore();
    INFO(built.bytes());
    CHECK(ContentStreamBuilder::check(built.bytes()));

    // THE CONTROLS, each a different defect -- and each must be refused by key.
    std::vector<std::string> controls{
        "q\n",                                   // q with no Q
        "Q\n",                                   // Q with no q
        "BT\nBT\nET\nET\n",                      // BT inside BT
        "ET\n",                                  // ET with no BT
        "BT\n",                                  // BT with no ET
        "[ 1 0 m ] TJ\n",                        // an operator inside an operand
        "1 0 0 rgb\n",                           // an operator Annex A does not have
        "BI /W 1 /H 1 ID x EI\n",                // an inline image: binary no tokenizer reads
        "(unterminated\n",                       // a token the lexer had to tolerate
        "[ 1 2\n",                               // an operand left open
    };
    std::string deep;
    for (int i = 0; i < 29; ++i) deep += "q\n";
    for (int i = 0; i < 29; ++i) deep += "Q\n";
    controls.push_back(deep);                    // 29 deep: one past Annex C's limit
    for (const std::string& stream : controls) {
        INFO("stream: " << stream);
        const core::Status checked = ContentStreamBuilder::check(stream);
        REQUIRE_FALSE(checked);
        CHECK(keyOf(checked.error()) == "error.pdf.contentStreamInvalid");
    }
    std::string limit;
    for (int i = 0; i < 28; ++i) limit += "q\n";
    for (int i = 0; i < 28; ++i) limit += "Q\n";
    CHECK(ContentStreamBuilder::check(limit));   // 28 is the limit, and is accepted
}

TEST_CASE("a highlight's appearance is a multiply form over its rectangle whatever order its corners are in", "[annotops][appearance]")
{
    Annotation acrobat = highlight(0, 100, 500, 80, 12);
    auto built = pdf::ops::AppearanceStreamBuilder::normal(acrobat);
    REQUIRE(built);
    const CosObject& form = built.value().dictionary;
    CHECK(form.isStream());   // a dictionary would be written with its data dropped
    CHECK(form.get("Type").text() == "XObject");
    CHECK(form.get("Subtype").text() == "Form");
    const CosObject& bbox = form.get("BBox");
    REQUIRE(bbox.items().size() == 4);
    CHECK(bbox.items()[0].numberValue() == acrobat.rect.left());
    CHECK(bbox.items()[1].numberValue() == acrobat.rect.bottom());
    CHECK(bbox.items()[2].numberValue() == acrobat.rect.right());
    CHECK(bbox.items()[3].numberValue() == acrobat.rect.top());
    const CosObject& gs = form.get("Resources").get("ExtGState").get("GS0");
    CHECK(gs.get("BM").text() == "Multiply");
    CHECK(gs.get("ca").numberValue() == 0.5);
    CHECK(gs.get("CA").numberValue() == 0.5);
    const std::string& data = built.value().data;
    INFO(data);
    CHECK(data.find("/GS0 gs") != std::string::npos);
    CHECK(data.find("1 1 0 rg") != std::string::npos);
    CHECK(ContentStreamBuilder::check(data));

    // ISO's figure order (counter-clockwise from bottom-left) draws the same convex quadrilateral.
    Annotation iso = acrobat;
    iso.quadPoints = {100, 500, 180, 500, 180, 512, 100, 512};
    auto isoBuilt = pdf::ops::AppearanceStreamBuilder::normal(iso);
    REQUIRE(isoBuilt);
    CHECK(isoBuilt.value().data == data);

    // No colour is Acrobat's yellow; no opacity is opaque.
    Annotation plain = acrobat;
    plain.color.clear();
    plain.opacity.reset();
    auto plainBuilt = pdf::ops::AppearanceStreamBuilder::normal(plain);
    REQUIRE(plainBuilt);
    CHECK(plainBuilt.value().data.find("1 1 0 rg") != std::string::npos);
    CHECK(plainBuilt.value().dictionary.get("Resources").get("ExtGState").get("GS0").get("ca").numberValue() == 1.0);
}

TEST_CASE("the appearance builder refuses what it cannot draw by key", "[annotops][appearance]")
{
    // A LINK since T14, which draws redaction marks and sounds (T11 moved it here from file attachments, T10 from
    // stamps, T8 from ink): every kind Stage 6 creates is drawn, and links are Stage 7's (`annot.linkTool`).
    Annotation link = core::newAnnotation(AnnotationSubtype::Link, 0, kAuthor, kNow);
    link.rect = core::PdfRect{core::PdfPoint{1, 2}, core::PdfSize{30, 10}};
    auto unsupported = pdf::ops::AppearanceStreamBuilder::normal(link);
    REQUIRE_FALSE(unsupported);
    CHECK(keyOf(unsupported.error()) == "error.pdf.appearanceUnsupported");
    CHECK_FALSE(pdf::ops::AppearanceStreamBuilder::supports(AnnotationSubtype::Link));
    CHECK(pdf::ops::AppearanceStreamBuilder::supports(AnnotationSubtype::Redact));
    CHECK(pdf::ops::AppearanceStreamBuilder::supports(AnnotationSubtype::Sound));
    CHECK(pdf::ops::AppearanceStreamBuilder::supports(AnnotationSubtype::FileAttachment));
    CHECK(pdf::ops::AppearanceStreamBuilder::supports(AnnotationSubtype::Stamp));
    CHECK(pdf::ops::AppearanceStreamBuilder::supports(AnnotationSubtype::Ink));
    CHECK(pdf::ops::AppearanceStreamBuilder::supports(AnnotationSubtype::Highlight));

    Annotation partial = highlight(0);
    partial.quadPoints.pop_back();                       // seven numbers: no whole quadrilateral
    auto geometry = pdf::ops::AppearanceStreamBuilder::normal(partial);
    REQUIRE_FALSE(geometry);
    CHECK(keyOf(geometry.error()) == "error.pdf.appearanceGeometry");

    Annotation twoComponents = highlight(0);
    twoComponents.color = {0.5, 0.5};
    auto colour = pdf::ops::AppearanceStreamBuilder::normal(twoComponents);
    REQUIRE_FALSE(colour);
    CHECK(keyOf(colour.error()) == "error.pdf.appearanceGeometry");
}

// ==========================================================================================
// A-24 -- an update holds exactly what the edit needs
// ==========================================================================================

TEST_CASE("an added highlight's update holds exactly the annotation its appearance and the page", "[annotops][A-24]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const long long pageObject = session->sink()->pages().pages[0].number;
    const Annotation added = highlight(0);
    REQUIRE(add(*session, {added}));

    const std::vector<long long> objects = objectsTheUpdateWrites(*session);
    INFO("objects written: " << objects.size());
    REQUIRE(objects.size() == 3);
    CHECK(std::count(objects.begin(), objects.end(), pageObject) == 1);

    const QByteArray saved = save(*session, QStringLiteral("A-24-text-a4-highlight.pdf"));
    CHECK(saved.startsWith(session->openedBytes));     // FROM THE PATH: the original is the prefix

    ReadBack file(saved);
    int annotations = 0;
    int forms = 0;
    for (const long long number : objects) {
        if (number == pageObject) continue;
        const CosObject object = file.object(number);
        if (object.get("Type").text() == "Annot") {
            ++annotations;
            CHECK(object.get("Subtype").text() == "Highlight");
            CHECK(object.get("P").reference().number == pageObject);
        } else if (object.isStream() && object.get("Subtype").text() == "Form") {
            ++forms;
        }
    }
    CHECK(annotations == 1);
    CHECK(forms == 1);
    // Page 1 lists it, and its other keys are the page's own.
    const CosObject page = file.object(pageObject);
    CHECK(page.get("Annots").items().size() == 1);
    REQUIRE(file.store.onPage(0).size() == 1);
    CHECK(file.store.at(file.store.onPage(0).front()).nm == added.nm);
}

TEST_CASE("an added annotation is written into the page's own indirect Annots array and not the page", "[annotops][A-24]")
{
    // Page 0 lists its annotations through an indirect array object; page 1 has none.
    int arrayObject = 0;
    int pageZero = 0;
    const std::string bytes = tests::annotatedDocument(2, [&](int page, tests::TinyPdf& pdf, int pageObject) -> std::string {
        if (page != 0) return {};
        pageZero = pageObject;
        const int square = pdf.add("<< /Type /Annot /Subtype /Square /Rect [ 10 10 50 50 ] /P " + std::to_string(pageObject) + " 0 R >>");
        arrayObject = pdf.add("[ " + std::to_string(square) + " 0 R ]");
        return std::to_string(arrayObject) + " 0 R";
    });
    auto session = tests::openBuilt(QStringLiteral("indirect-annots.pdf"), bytes);
    REQUIRE(session);
    REQUIRE(add(*session, {highlight(0)}));

    const std::vector<long long> objects = objectsTheUpdateWrites(*session);
    REQUIRE(objects.size() == 3);
    CHECK(std::count(objects.begin(), objects.end(), arrayObject) == 1);
    CHECK(std::count(objects.begin(), objects.end(), pageZero) == 0);   // the page dictionary is untouched

    const QByteArray saved = save(*session, QStringLiteral("A-24-indirect-annots.pdf"));
    ReadBack file(saved);
    CHECK(file.object(arrayObject).items().size() == 2);
    CHECK(file.store.onPage(0).size() == 2);
}

TEST_CASE("a rotation and an annotation on one page write that page once", "[annotops][A-24]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    REQUIRE(session->rotate(0));
    REQUIRE(add(*session, {highlight(0)}));
    const std::vector<long long> objects = objectsTheUpdateWrites(*session);
    CHECK(objects.size() == 3);
    const QByteArray saved = save(*session, QStringLiteral("A-24-rotate-and-highlight.pdf"));
    ReadBack file(saved);
    CHECK(file.walk.pages[0].effectiveRotate() == 90);
    CHECK(file.store.onPage(0).size() == 1);
}

// ==========================================================================================
// A-25 -- desired state: an undo before a save writes nothing; one after a save writes it back
// ==========================================================================================

TEST_CASE("an annotation added and undone before a save writes nothing", "[annotops][A-25]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const QByteArray hash = session->journal->document().modelHash();
    REQUIRE(add(*session, {highlight(0), highlight(0, 72, 600)}));
    CHECK(session->journal->document().modelHash() != hash);   // the model hash SEES an annotation
    REQUIRE(session->journal->undo());
    CHECK(session->journal->document().modelHash() == hash);
    CHECK_FALSE(session->journal->isDirty());
    auto delta = session->sink()->delta();
    REQUIRE(delta);
    CHECK(delta.value().isEmpty());
    auto result = session->coordinator->save(*session->journal, session->path);
    REQUIRE_FALSE(result);
    CHECK(keyOf(result.error()) == "error.save.nothingToSave");
    CHECK(tests::sha256(tests::readAllBytes(session->path)) == tests::sha256(session->openedBytes));
}

TEST_CASE("an annotation added saved and deleted writes the page back and no annotation", "[annotops][A-25]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const long long pageObject = session->sink()->pages().pages[0].number;
    const CosObject pageBefore = ReadBack(session->openedBytes).object(pageObject);
    REQUIRE_FALSE(pageBefore.has("Annots"));

    const Annotation added = highlight(0);
    REQUIRE(add(*session, {added}));
    const QByteArray first = save(*session, QStringLiteral("A-25-added.pdf"));
    REQUIRE(remove(*session, {added.id}));

    // The second update is the page alone -- its /Annots back to what it was (absent) -- and nothing else.
    const std::vector<long long> objects = objectsTheUpdateWrites(*session);
    CHECK(objects == std::vector<long long>{pageObject});
    const QByteArray second = save(*session, QStringLiteral("A-25-deleted.pdf"));
    CHECK(second.startsWith(first));
    ReadBack file(second);
    CHECK(file.object(pageObject) == pageBefore);
    CHECK(file.store.onPage(0).empty());

    // And undoing the delete after that save lists the SAME object again -- no new annotation written.
    REQUIRE(session->journal->undo());
    const std::vector<long long> again = objectsTheUpdateWrites(*session);
    CHECK(again == std::vector<long long>{pageObject});
    const QByteArray third = save(*session, QStringLiteral("A-25-undone.pdf"));
    ReadBack restored(third);
    REQUIRE(restored.store.onPage(0).size() == 1);
    CHECK(restored.store.at(restored.store.onPage(0).front()).nm == added.nm);
    CHECK(ReadBack(first).store.at(ReadBack(first).store.onPage(0).front()).id
          == restored.store.at(restored.store.onPage(0).front()).id);
}

TEST_CASE("deleting annotations the file holds writes only listings and an undo lists the same objects again", "[annotops][A-25]")
{
    auto session = tests::openCopy("ordinary/annots-mixed.pdf");
    REQUIRE(session);
    const core::AnnotationStore& store = session->journal->document().annotations();
    const std::vector<AnnotationId> page0 = idsOnPage(store, 0);
    REQUIRE(page0.size() >= 2);
    const auto listingBefore = session->sink()->desiredListing(0);

    REQUIRE(remove(*session, page0));
    CHECK(store.onPage(0).empty());
    // Only page 0's dictionary (or its array): every annotation object stays as it was on disk.
    CHECK(objectsTheUpdateWrites(*session).size() == 1);
    save(*session, QStringLiteral("A-25-mixed-deleted.pdf"));

    REQUIRE(session->journal->undo());
    const std::vector<long long> objects = objectsTheUpdateWrites(*session);
    CHECK(objects.size() == 1);
    const QByteArray restored = save(*session, QStringLiteral("A-25-mixed-restored.pdf"));
    ReadBack file(restored);
    const auto& listingAfter = session->sink()->desiredListing(0);
    REQUIRE(listingAfter.size() == listingBefore.size());
    for (std::size_t i = 0; i < listingBefore.size(); ++i) CHECK(listingAfter[i].sameEntry(listingBefore[i]));
    CHECK(idsOnPage(file.store, 0) == page0);
}

TEST_CASE("a direct annotation and one two pages list are deleted and restored across a save", "[annotops][A-25]")
{
    auto session = tests::openCopy("ordinary/annots-direct.pdf");
    REQUIRE(session);
    const core::AnnotationStore& store = session->journal->document().annotations();
    std::optional<AnnotationId> direct;
    std::optional<AnnotationId> shared;
    for (const Annotation& a : store.all()) {
        if (a.id.kind == AnnotationId::Kind::Direct && !direct) direct = a.id;
        if (a.id.kind == AnnotationId::Kind::Object && store.placementsOf(a.id).size() > 1) shared = a.id;
    }
    REQUIRE(direct);
    REQUIRE(shared);
    const std::vector<std::vector<pdf::ops::MutationSinkImpl::ListedEntry>> before = [&] {
        std::vector<std::vector<pdf::ops::MutationSinkImpl::ListedEntry>> out;
        for (int page = 0; page < session->sink()->pageCount(); ++page) out.push_back(session->sink()->desiredListing(page));
        return out;
    }();
    const QByteArray hashBefore = session->journal->document().modelHash();

    REQUIRE(remove(*session, {*direct, *shared}));
    CHECK_FALSE(store.find(*shared));
    CHECK(store.placementsOf(*shared).empty());
    save(*session, QStringLiteral("A-25-direct-deleted.pdf"));

    REQUIRE(session->journal->undo());
    CHECK(session->journal->document().modelHash() == hashBefore);
    const QByteArray restored = save(*session, QStringLiteral("A-25-direct-restored.pdf"));
    for (int page = 0; page < session->sink()->pageCount(); ++page) {
        INFO("page " << page);
        const auto& now = session->sink()->desiredListing(page);
        REQUIRE(now.size() == before[static_cast<std::size_t>(page)].size());
        for (std::size_t i = 0; i < now.size(); ++i) CHECK(now[i].sameEntry(before[static_cast<std::size_t>(page)][i]));
    }
    // The direct one is direct still, with its value; the shared one is listed by both pages again.
    ReadBack file(restored);
    CHECK(file.store.placementsOf(*shared).size() == 2);
    // And the next operations see the names the model gave at open, though the save moved nothing.
    REQUIRE(remove(*session, {*direct}));
    REQUIRE(session->journal->undo());
    CHECK(objectsTheUpdateWrites(*session).empty());
}

// ==========================================================================================
// A-26 -- an edit preserves what it does not model (the rule, at `editedDictionary`; the command is T5's)
// ==========================================================================================

TEST_CASE("an edit overwrites only the modelled keys it changes and keeps every other one", "[annotops][A-26]")
{
    std::size_t annotations = 0;
    std::size_t withRichText = 0;
    for (const char* name : {"ordinary/annots-unknown-keys.pdf", "ordinary/annots-mixed.pdf"}) {
        INFO("file: " << name);
        ReadBack file(tests::readAllBytes(tests::corpusFile(name)));
        for (const Annotation& before : file.store.all()) {
            if (before.id.kind != AnnotationId::Kind::Object) continue;
            INFO("annotation " << before.id.toString().toStdString());
            const CosObject onDisk = file.object(before.id.object);
            ++annotations;

            // Nothing changed: the dictionary, value for value.
            CHECK(pdf::ops::AnnotationOps::editedDictionary(onDisk, before, before) == onDisk);

            // The colour: /C, and nothing else.
            Annotation recoloured = before;
            recoloured.color = {0.0, 0.5, 1.0};
            const CosObject edited = pdf::ops::AnnotationOps::editedDictionary(onDisk, before, recoloured);
            REQUIRE(edited.entries().size() == onDisk.entries().size() + (onDisk.has("C") ? 0 : 1));
            for (const CosObject::Entry& entry : onDisk.entries()) {
                INFO("key /" << entry.key);
                if (entry.key == "C") continue;
                REQUIRE(edited.find(entry.key) != nullptr);
                CHECK(*edited.find(entry.key) == *onDisk.find(entry.key));
            }
            CHECK(edited.get("C").items().size() == 3);

            // The text: /Contents, and /RC exactly when the dictionary has one; /AP never.
            Annotation retyped = before;
            retyped.contents = QStringLiteral("New text & <more>");
            const CosObject rewritten = pdf::ops::AnnotationOps::editedDictionary(onDisk, before, retyped);
            CHECK(rewritten.has("RC") == onDisk.has("RC"));
            if (onDisk.has("RC")) {
                ++withRichText;
                const QString rc = pdf::ops::EmbeddedFileReader::decodeTextString(rewritten.get("RC").text());
                CHECK(rc.contains(QStringLiteral("New text &amp; &lt;more&gt;")));
            }
            if (onDisk.has("AP")) CHECK(*rewritten.find("AP") == *onDisk.find("AP"));
            for (const char* unmodelled : {"PieceInfo", "RawPageTestKey"}) {
                if (onDisk.has(unmodelled)) CHECK(*rewritten.find(unmodelled) == *onDisk.find(unmodelled));
            }
        }
    }
    CHECK(annotations >= 10);
    CHECK(withRichText >= 1);   // annots-unknown-keys.pdf carries /RC: the rewrite was exercised
}

// ==========================================================================================
// A-27 -- everything RawPage writes has an identity
// ==========================================================================================

TEST_CASE("a created highlight is saved with its NM dates author flags and page", "[annotops][A-27]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const Annotation one = highlight(0);
    const Annotation two = highlight(0, 72, 650);
    CHECK(one.nm != two.nm);
    REQUIRE(add(*session, {one, two}));
    const QByteArray saved = save(*session, QStringLiteral("A-27-identity.pdf"));
    ReadBack file(saved);
    REQUIRE(file.store.onPage(0).size() == 2);
    std::set<QString> names;
    for (const std::size_t index : file.store.onPage(0)) {
        const Annotation& a = file.store.at(index);
        INFO("nm " << a.nm.toStdString());
        CHECK(QRegularExpression(QStringLiteral("^[0-9a-f]{32}$")).match(a.nm).hasMatch());
        names.insert(a.nm);
        CHECK(a.author == kAuthor);
        CHECK(a.created.utcSeconds == kNow);
        CHECK(a.modified.utcSeconds == kNow);
        CHECK(a.flags == core::AnnotationFlag::Print);
        CHECK(a.hasAppearance);
        CHECK(a.subtype == AnnotationSubtype::Highlight);
        CHECK(a.color == std::vector<double>{1.0, 1.0, 0.0});
        CHECK(a.opacity == 0.5);
        CHECK(a.contents == QStringLiteral("Highlighted by a test"));
        CHECK((a.quadPoints == one.quadPoints || a.quadPoints == two.quadPoints));
        const CosObject dictionary = file.object(a.id.object);
        CHECK(dictionary.get("P").reference().number == file.walk.pages[0].number);
    }
    CHECK(names == std::set<QString>{one.nm, two.nm});

    // A non-ASCII author is written as UTF-16 and read back exactly.
    auto unicode = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(unicode);
    Annotation zoe = highlight(0);
    zoe.author = QStringLiteral("Zoë Müller 林");
    REQUIRE(add(*unicode, {zoe}));
    ReadBack back(save(*unicode, QStringLiteral("A-27-unicode-author.pdf")));
    REQUIRE(back.store.onPage(0).size() == 1);
    CHECK(back.store.at(back.store.onPage(0).front()).author == zoe.author);
}

// ==========================================================================================
// A-30 -- save verifies annotations too
// ==========================================================================================

TEST_CASE("a save whose composed bytes lost the new annotation fails verify before any replace", "[annotops][A-30]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    REQUIRE(add(*session, {highlight(0)}));
    const std::size_t original = static_cast<std::size_t>(session->openedBytes.size());

    // THE FAULT: the update's /Annots key is renamed, so the page lists nothing -- every other check
    // (prefix, parse, pages, rotation) still passes.
    pdf::ops::RevisionWriter::setComposeFaultForTesting([original](QByteArray& bytes) {
        const qsizetype at = bytes.indexOf("/Annots", static_cast<qsizetype>(original));
        if (at >= 0) bytes[at + 1] = 'X';
    });
    auto result = session->coordinator->save(*session->journal, session->path);
    pdf::ops::RevisionWriter::setComposeFaultForTesting({});
    REQUIRE_FALSE(result);
    CHECK(keyOf(result.error()) == "error.save.verifyFailed");
    CHECK(result.error().context == QStringLiteral("annotations:1"));
    CHECK(session->durableFiles.replaceCalls == 0);
    CHECK(session->strayTemporaries().isEmpty());
    CHECK(tests::sha256(tests::readAllBytes(session->path)) == tests::sha256(session->openedBytes));
    CHECK(session->journal->isDirty());

    // THE CONTROL: the same save without the fault is written.
    save(*session, QStringLiteral("A-30-control.pdf"));

    // And a fault in the APPEARANCE -- an operator Annex A does not have -- is refused the same way.
    auto second = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(second);
    REQUIRE(add(*second, {highlight(0)}));
    pdf::ops::RevisionWriter::setComposeFaultForTesting([original](QByteArray& bytes) {
        const qsizetype at = bytes.indexOf("\nf\n", static_cast<qsizetype>(original));
        if (at >= 0) bytes[at + 1] = 'z';
    });
    auto broken = second->coordinator->save(*second->journal, second->path);
    pdf::ops::RevisionWriter::setComposeFaultForTesting({});
    REQUIRE_FALSE(broken);
    CHECK(keyOf(broken.error()) == "error.save.verifyFailed");
    CHECK(tests::sha256(tests::readAllBytes(second->path)) == tests::sha256(second->openedBytes));
}

// ==========================================================================================
// Refusals leave no trace; the per-page pre-flight; numbering past a dangling reference
// ==========================================================================================

TEST_CASE("an annotation the sink cannot write is refused by the journal with nothing changed", "[annotops][refuse]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const QByteArray hash = session->journal->document().modelHash();
    // A STAMP since T8, which draws ink: a kind this build cannot draw yet (T10's).
    Annotation stamp = core::newAnnotation(AnnotationSubtype::Stamp, 0, kAuthor, kNow);
    stamp.rect = core::PdfRect{core::PdfPoint{10, 10}, core::PdfSize{60, 20}};
    // A set: one the sink can write and one it cannot -- the whole set is refused.
    CHECK_FALSE(add(*session, {highlight(0), stamp}));
    CHECK(session->journal->document().modelHash() == hash);
    CHECK(session->journal->historySize() == 0);
    auto delta = session->sink()->delta();
    REQUIRE(delta);
    CHECK(delta.value().isEmpty());

    // An annotation whose id the document already holds, a page past the end, an empty set.
    const Annotation first = highlight(0);
    REQUIRE(add(*session, {first}));
    const QByteArray afterOne = session->journal->document().modelHash();
    CHECK_FALSE(add(*session, {first}));
    CHECK_FALSE(add(*session, {highlight(99)}));
    CHECK_FALSE(add(*session, {}));
    CHECK_FALSE(remove(*session, {AnnotationId::ofObject(999999)}));
    CHECK_FALSE(remove(*session, {first.id, first.id}));
    CHECK(session->journal->document().modelHash() == afterOne);
    CHECK(session->journal->historySize() == 1);
}

TEST_CASE("the sink refuses a placement out of range or an entry that is not an annotation without trace", "[annotops][refuse]")
{
    const std::string bytes = tests::annotatedDocument(1, [](int, tests::TinyPdf& pdf, int pageObject) -> std::string {
        const int square = pdf.add("<< /Type /Annot /Subtype /Square /Rect [ 1 1 5 5 ] /P " + std::to_string(pageObject) + " 0 R >>");
        return "[ " + std::to_string(square) + " 0 R 42 ]";   // the second entry is an integer
    });
    auto session = tests::openBuilt(QStringLiteral("junk-entry.pdf"), bytes);
    REQUIRE(session);
    pdf::ops::MutationSinkImpl& sink = *session->sink();
    CHECK_FALSE(sink.removeAnnotations({{0, 1}}));     // the integer: no annotation to drop
    CHECK_FALSE(sink.removeAnnotations({{0, 2}}));     // past the end
    CHECK_FALSE(sink.removeAnnotations({{0, 0}, {0, 0}, {0, 0}}));   // the third has nothing left
    CHECK_FALSE(sink.insertAnnotations({{0, 3}}, {highlight(0)}));
    CHECK_FALSE(sink.insertAnnotations({{1, 0}}, {highlight(0)}));
    CHECK_FALSE(sink.insertAnnotations({{0, 0}, {0, 0}}, {highlight(0)}));   // counts differ
    auto delta = sink.delta();
    REQUIRE(delta);
    CHECK(delta.value().isEmpty());
    CHECK(sink.desiredListing(0).size() == 2);

    // The integer survives a delete of the annotation before it, as written.
    REQUIRE(remove(*session, idsOnPage(session->journal->document().annotations(), 0)));
    ReadBack file(save(*session, QStringLiteral("refuse-junk-entry.pdf")));
    const CosObject page = file.object(file.walk.pages[0].number);
    REQUIRE(page.get("Annots").items().size() == 1);
    CHECK(page.get("Annots").items()[0].integerValue() == 42);
}

TEST_CASE("an annotation command on a page whose readers disagree is refused by key", "[annotops][D6-3]")
{
    auto session = tests::openBuilt(QStringLiteral("disagree.pdf"),
                                    tests::readAllBytes(tests::corpusFile("ordinary/annots-threads.pdf")).toStdString(),
                                    [](tests::CosBackend& backend) { backend.plantAnnotationDisagreement(1); });
    REQUIRE(session);
    const core::AnnotationStore& store = session->journal->document().annotations();
    auto refused = add(*session, {highlight(1)});
    REQUIRE_FALSE(refused);
    CHECK(keyOf(refused.error()) == "error.edit.annotationsDisagree");
    CHECK(refused.error().context == QStringLiteral("2"));
    const std::vector<AnnotationId> page2 = idsOnPage(store, 1);
    REQUIRE_FALSE(page2.empty());
    auto refusedDelete = remove(*session, {page2.front()});
    REQUIRE_FALSE(refusedDelete);
    CHECK(keyOf(refusedDelete.error()) == "error.edit.annotationsDisagree");
    CHECK(session->journal->historySize() == 0);
    CHECK(add(*session, {highlight(0)}));      // page 1 agrees, and is editable

    // An encrypted reading refuses every annotation command up front.
    auto encrypted = tests::openBuilt(QStringLiteral("encrypted-planted.pdf"),
                                      tests::readAllBytes(tests::corpusFile("ordinary/text-a4.pdf")).toStdString(),
                                      [](tests::CosBackend& backend) { backend.plantReading(core::CosReading::Encrypted); });
    CHECK_FALSE(encrypted);   // no sink: the first-party writer refuses it, as the shipped backend does
}

TEST_CASE("a created annotation is numbered past every number the file references", "[annotops][I-12]")
{
    // The page refers to object 40, which the file never defines -- /Size says 6.
    const std::string bytes = tests::annotatedDocument(1, [](int, tests::TinyPdf&, int) -> std::string {
        return "[ 40 0 R ]";
    });
    auto session = tests::openBuilt(QStringLiteral("dangling.pdf"), bytes);
    REQUIRE(session);
    REQUIRE(add(*session, {highlight(0)}));
    for (const long long number : objectsTheUpdateWrites(*session)) {
        if (number == session->sink()->pages().pages[0].number) continue;
        CHECK(number > 40);
    }
    save(*session, QStringLiteral("I-12-dangling.pdf"));
}

// ==========================================================================================
// The store's half: insert and remove, all or nothing
// ==========================================================================================

TEST_CASE("the store lists inserts in order and removes all or nothing", "[annotops][store]")
{
    const std::string bytes = tests::annotatedDocument(2, [](int page, tests::TinyPdf& pdf, int pageObject) -> std::string {
        const int a = pdf.add("<< /Type /Annot /Subtype /Square /Rect [ 1 1 5 5 ] /P " + std::to_string(pageObject) + " 0 R >>");
        if (page == 1) return "[ " + std::to_string(a) + " 0 R ]";
        const int shared = pdf.add("<< /Type /Annot /Subtype /Circle /Rect [ 2 2 6 6 ] /P " + std::to_string(pageObject) + " 0 R >>");
        return "[ " + std::to_string(a) + " 0 R " + std::to_string(shared) + " 0 R ]";
    });
    auto reading = tests::readAnnotations(bytes);
    REQUIRE(reading);
    core::AnnotationStore store = core::AnnotationStore::fromReading(std::move(*reading));
    REQUIRE(store.size() == 3);

    const Annotation h = highlight(1);
    REQUIRE(store.insert({{{1, 0}, h}, {{1, 2}, h}}));      // listed twice, ONE annotation
    CHECK(store.size() == 4);
    CHECK(store.placementsOf(h.id) == std::vector<core::AnnotationPlacement>{{1, 0}, {1, 2}});

    CHECK_FALSE(store.insert({{{1, 0}, highlight(1)}, {{1, 9}, highlight(1)}}));   // the second is out of range
    CHECK(store.size() == 4);
    CHECK_FALSE(store.remove({{1, 0}, {1, 5}}));
    CHECK(store.listing(1).size() == 3);

    REQUIRE(store.remove({{1, 2}}));
    CHECK(store.find(h.id));                                // still listed once
    REQUIRE(store.remove({{1, 0}}));
    CHECK_FALSE(store.find(h.id));                          // listed nowhere: gone
    CHECK(store.size() == 3);

    // A refused store refuses both.
    core::AnnotationStore refused = core::AnnotationStore::refused(
        core::Error{core::ErrorCode::PermissionDenied, QLatin1String("error.edit.encrypted"), QString()});
    CHECK_FALSE(refused.insert({{{0, 0}, highlight(0)}}));
    CHECK_FALSE(refused.remove({{0, 0}}));
}

// ==========================================================================================
// A-29 -- recovery replays annotation commands against the file as opened
// ==========================================================================================

TEST_CASE("recovery replays annotation commands to the same model and the same bytes", "[annotops][A-29][recovery]")
{
    auto session = tests::openCopy("ordinary/annots-mixed.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto recovery = core::RecoveryJournal::open(root.path(), session->path, session->journal->document().id(),
                                                &session->durableFiles);
    REQUIRE(recovery->isRecoverable());
    session->journal->setRecorder(recovery.get());

    const core::AnnotationStore& store = session->journal->document().annotations();
    const Annotation created = highlight(0);
    REQUIRE(add(*session, {created, highlight(1)}));
    REQUIRE(remove(*session, {idsOnPage(store, 0).front(), created.id}));
    REQUIRE(add(*session, {highlight(0, 50, 50)}));
    REQUIRE(session->journal->undo());

    const QByteArray expectedModel = session->journal->document().modelHash();
    auto expected = pdf::ops::RevisionWriter::compose(*session->sink(), false);
    REQUIRE(expected);
    // No line names an object number a compose allocated (D6-4): every id is the file's or an /NM.
    const QByteArray lines = tests::readAllBytes(QDir(recovery->directory()).filePath("journal.ndjson"));
    for (const long long number : expected.value().objectsWritten) {
        CHECK_FALSE(lines.contains(QStringLiteral("\"obj:%1\"").arg(number).toUtf8()));
    }

    recovery.reset();
    session->journal->setRecorder(nullptr);
    const auto candidates = core::RecoveryJournal::discover(root.path());
    REQUIRE(candidates.size() == 1);
    auto fresh = tests::openCopy("ordinary/annots-mixed.pdf");
    REQUIRE(fresh);
    auto report = core::RecoveryJournal::replay(candidates.front().directory, *fresh->journal);
    REQUIRE(report);
    CHECK_FALSE(report.value().stoppedAtStep.has_value());
    CHECK(fresh->journal->document().modelHash() == expectedModel);
    CHECK(fresh->journal->canRedo());
    auto replayed = pdf::ops::RevisionWriter::compose(*fresh->sink(), false);
    REQUIRE(replayed);
    CHECK(tests::sha256(replayed.value().bytes) == tests::sha256(expected.value().bytes));
}

// ==========================================================================================
// Stage 6 T4 (D6-2) -- what is shown is what is saved, at the level of BYTES, headless
// ==========================================================================================
// The engine's half -- the same pixels -- is `tests/render/tst_annotdisplay.cpp`. This half runs under
// `portable-asan` too: the update `showEdits` hands the engine after each command is byte for byte the
// update the next save appends, over the shipped backend's compose and verify (`RevisionWriter`).

namespace {

void showLikeTheProduct(tests::SaveSession& session)
{
    tests::SaveSession* self = &session;
    session.journal->setShowHandler([self](const core::Scope&) { return self->backend.showEdits(self->handle, {}); });
}

} // namespace

TEST_CASE("the update shown after every command is the update the save appends", "[annotops][annotdisplay][A-34]")
{
    auto session = tests::openCopy("ordinary/annots-mixed.pdf");
    REQUIRE(session);
    showLikeTheProduct(*session);
    const int showsBefore = session->backend.shows();

    REQUIRE(add(*session, {highlight(0), highlight(0, 72, 600)}));
    REQUIRE(session->rotate(0));
    REQUIRE(add(*session, {highlight(1)}));
    CHECK(session->backend.shows() - showsBefore == 3);   // ONE per command
    const QByteArray shown = session->backend.shownUpdate(session->handle);
    REQUIRE_FALSE(shown.isEmpty());

    const QByteArray saved = save(*session, QStringLiteral("T4-shown-is-saved.pdf"));
    REQUIRE(saved.startsWith(session->openedBytes));
    CHECK(tests::sha256(saved.mid(session->openedBytes.size())) == tests::sha256(shown));

    // AN UNDO IS SHOWN TOO, and an undo back to the file on disk shows no update at all.
    const QByteArray afterSave = tests::readAllBytes(session->path);
    REQUIRE(session->journal->undo());
    CHECK_FALSE(session->backend.shownUpdate(session->handle).isEmpty());
    REQUIRE(session->journal->redo());
    CHECK(session->backend.shownUpdate(session->handle).isEmpty());
    CHECK(tests::readAllBytes(session->path) == afterSave);
}

TEST_CASE("an edit whose update fails verify is refused when it is made and leaves no trace", "[annotops][annotdisplay][A-39]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    showLikeTheProduct(*session);
    const QByteArray hash = session->journal->document().modelHash();
    const std::size_t original = static_cast<std::size_t>(session->openedBytes.size());

    // A-30's fault, now at EDIT time rather than at the save.
    pdf::ops::RevisionWriter::setComposeFaultForTesting([original](QByteArray& bytes) {
        const qsizetype at = bytes.indexOf("/Annots", static_cast<qsizetype>(original));
        if (at >= 0) bytes[at + 1] = 'X';
    });
    const core::Status refused = add(*session, {highlight(0)});
    pdf::ops::RevisionWriter::setComposeFaultForTesting({});
    REQUIRE_FALSE(refused);
    CHECK(keyOf(refused.error()) == "error.edit.notShown");
    CHECK(refused.error().context.startsWith(QStringLiteral("error.save.verifyFailed")));
    CHECK_FALSE(session->journal->canUndo());
    CHECK_FALSE(session->journal->isDirty());
    CHECK(session->journal->document().modelHash() == hash);
    // The SINK was taken back as well as the model: the next save has nothing to write.
    auto delta = session->sink()->delta();
    REQUIRE(delta);
    CHECK(delta.value().isEmpty());

    // THE CONTROL: unfaulted, the same edit is made.
    REQUIRE(add(*session, {highlight(0)}));
    CHECK(session->journal->canUndo());
}

// Stage 6 T4, the named work of A2 at Stage 6: what an annotation command says it repaints.
TEST_CASE("an annotation command's scope is its annotations' rectangles and the whole page where a reader draws elsewhere", "[annotops][annotdisplay]")
{
    Annotation plain = highlight(0, 72, 700, 200, 14);
    Annotation onAnother = highlight(2, 100, 100, 50, 10);
    const core::Scope scope = AddAnnotationCommand({plain, onAnother}).scope();
    CHECK(scope.aspect() == core::Scope::Aspect::Annotations);
    CHECK(scope.pageIndices() == std::vector<int>{0, 2});
    REQUIRE(scope.regionsOf(0) != nullptr);
    REQUIRE(scope.regionsOf(0)->size() == 1);
    // The rectangle, padded a point for anti-aliasing.
    const core::PdfRect padded = scope.regionsOf(0)->front();
    CHECK(padded.left() == 71.0);
    CHECK(padded.bottom() == 699.0);
    CHECK(padded.size.width == 202.0);
    CHECK(padded.size.height == 16.0);

    // DRAWN ELSEWHERE, OR NOT KNOWN: the whole page.
    Annotation noZoom = highlight(1);
    noZoom.flags |= core::AnnotationFlag::NoZoom;
    Annotation noRotate = highlight(3);
    noRotate.flags |= core::AnnotationFlag::NoRotate;
    Annotation withPopup = highlight(4);
    withPopup.popup = core::AnnotationId::ofObject(77);
    Annotation popup = core::newAnnotation(AnnotationSubtype::Popup, 5, kAuthor, kNow);
    popup.rect = core::PdfRect{core::PdfPoint{0, 0}, core::PdfSize{100, 100}};
    Annotation empty = highlight(6);
    empty.rect = core::PdfRect{};
    for (const Annotation& a : {noZoom, noRotate, withPopup, popup, empty}) {
        INFO("page " << a.page);
        const core::Scope whole = AddAnnotationCommand({a, plain}).scope();
        CHECK(whole.touchesPage(a.page));
        CHECK(whole.regionsOf(a.page) == nullptr);
        CHECK(whole.regionsOf(0) != nullptr);   // the plain one beside it keeps its rectangle
    }
}
