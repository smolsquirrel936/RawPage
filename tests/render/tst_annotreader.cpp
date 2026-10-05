// tst_annotreader -- Stage 6 T2, guide cases A-17, A-18 and A-19 (`Stage6-Annotations-Testing-Guide.md`
// §5.4): `pdf/ops/AnnotationReader` through the SHIPPED backend, graded against the generator's labels
// (`corpus/labels/annots-*.json`) and held to agree with the engine page by page.
//
//   * **A-17.** Every labelled annotation read back field by field -- none missing, none invented. The
//     ORACLE IS THE GENERATOR (rule 7): the labels are what `corpus/make-annots.py` wrote, never what
//     PDFium's `FPDFAnnot_*` getters say. Its control removes one label entry and requires the
//     comparison to report the annotation it no longer describes as INVENTED.
//   * **A-18.** Over every corpus file the engine opens, the two readers agree on every page's
//     `/Annots` -- count, and per entry dictionary-ness, subtype and rectangle -- and a planted
//     disagreement makes exactly that page read-only (`Permissions::mayAnnotate`), refused by key with
//     the page named. **The plant is on `annots-threads.pdf` page 2**, not `annots-mixed.pdf` as the
//     guide wrote: `annots-mixed.pdf`'s page 2 carries no annotation, so a skipped entry there cannot
//     exist (recorded as a deviation at A-18).
//   * **A-19.** An encrypted document's store refuses by key and no annotation edit is permitted; the
//     annotation `annots-direct.pdf` writes with no `/P` and lists on no page is reported in the log and
//     absent from the store.
//
// WHAT THIS DOES NOT COVER (convention 19): the agreement is judged on the facts both readers expose
// -- count, subtype, rectangle -- so two readings that agree there and differ in a colour or a date
// would pass A-18; A-17 is what holds every field, against the generator, for the labelled files only.

#include <catch2/catch_test_macros.hpp>

#include "core/Log.h"
#include "core/backend.h"
#include "core/doc/AnnotationStore.h"
#include "core/doc/Permissions.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/render/PdfiumDocument.h"
#include "tests/unit/TinyPdf.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;

namespace {

QString corpusFile(const QString& relative)
{
    return QStringLiteral(RAWPAGE_CORPUS_DIR "/") + relative;
}

QJsonObject label(const QString& relative)
{
    QFile file(corpusFile(relative));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool near(double a, double b) { return std::fabs(a - b) <= 1e-6; }

bool sameNumbers(const std::vector<double>& ours, const QJsonValue& theirs)
{
    const QJsonArray array = theirs.toArray();
    if (static_cast<qsizetype>(ours.size()) != array.size()) return false;
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!near(ours[static_cast<std::size_t>(i)], array[i].toDouble())) return false;
    }
    return true;
}

QString xmlEscaped(QString text)
{
    text.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    text.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    text.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return text;
}

// One label entry against the annotation the store resolved its listing entry to. Each problem is a
// sentence naming the field, so a failure says what differed rather than that something did.
void compareEntry(const QJsonObject& want, const core::Annotation& got, const QString& where,
                  QStringList& problems)
{
    auto bad = [&](const QString& field, const QString& detail = QString()) {
        problems << QStringLiteral("%1: %2 %3").arg(where, field, detail);
    };
    const QString subtype = got.subtype == core::AnnotationSubtype::Unknown
                                ? got.subtypeName
                                : QString(core::annotationSubtypeName(got.subtype));
    if (subtype != want.value(QStringLiteral("subtype")).toString()) bad(QStringLiteral("subtype"), subtype);

    const QJsonArray rect = want.value(QStringLiteral("rect")).toArray();
    if (rect.size() != 4 || !near(got.rect.left(), rect[0].toDouble()) || !near(got.rect.bottom(), rect[1].toDouble())
        || !near(got.rect.right(), rect[2].toDouble()) || !near(got.rect.top(), rect[3].toDouble()))
        bad(QStringLiteral("rect"));
    if (static_cast<double>(got.flags) != want.value(QStringLiteral("flags")).toDouble()) bad(QStringLiteral("flags"));

    struct Numbers { const char* key; const std::vector<double>* ours; };
    for (const Numbers& n : {Numbers{"quadPoints", &got.quadPoints}, Numbers{"vertices", &got.vertices},
                             Numbers{"line", &got.line}, Numbers{"color", &got.color},
                             Numbers{"interiorColor", &got.interiorColor}, Numbers{"callout", &got.callout},
                             Numbers{"rectDifferences", &got.rectDifferences}}) {
        const QJsonValue value = want.value(QLatin1String(n.key));
        if (value.isUndefined() ? !n.ours->empty() : !sameNumbers(*n.ours, value)) bad(QLatin1String(n.key));
    }
    const QJsonArray ink = want.value(QStringLiteral("inkList")).toArray();
    if (static_cast<qsizetype>(got.inkList.size()) != ink.size()) bad(QStringLiteral("inkList"));
    else {
        for (qsizetype s = 0; s < ink.size(); ++s) {
            if (!sameNumbers(got.inkList[static_cast<std::size_t>(s)], ink[s])) bad(QStringLiteral("inkList"));
        }
    }

    struct Optional { const char* key; const std::optional<double>* ours; };
    for (const Optional& o : {Optional{"opacity", &got.opacity}, Optional{"borderWidth", &got.borderWidth}}) {
        const QJsonValue value = want.value(QLatin1String(o.key));
        if (value.isUndefined() != !o.ours->has_value()
            || (o.ours->has_value() && !near(**o.ours, value.toDouble())))
            bad(QLatin1String(o.key));
    }

    struct Text { const char* key; const QString* ours; };
    for (const Text& t : {Text{"nm", &got.nm}, Text{"author", &got.author}, Text{"subject", &got.subject},
                          Text{"contents", &got.contents}, Text{"intent", &got.intent}, Text{"icon", &got.icon},
                          Text{"defaultAppearance", &got.defaultAppearance}, Text{"stateModel", &got.stateModel},
                          Text{"state", &got.state}, Text{"created", &got.created.raw},
                          Text{"modified", &got.modified.raw}}) {
        if (*t.ours != want.value(QLatin1String(t.key)).toString()) bad(QLatin1String(t.key), *t.ours);
    }
    if (got.created.utcIso() != want.value(QStringLiteral("createdUtc")).toString()) bad(QStringLiteral("createdUtc"));
    if (got.modified.utcIso() != want.value(QStringLiteral("modifiedUtc")).toString()) bad(QStringLiteral("modifiedUtc"));
    if (want.contains(QStringLiteral("richText"))
        && !got.richText.contains(xmlEscaped(want.value(QStringLiteral("richText")).toString())))
        bad(QStringLiteral("richText"));
    if (!want.contains(QStringLiteral("richText")) && !got.richText.isEmpty()) bad(QStringLiteral("richText"));

    const QJsonObject irt = want.value(QStringLiteral("inReplyTo")).toObject();
    if (irt.isEmpty() ? got.inReplyTo.has_value()
                      : (!got.inReplyTo
                         || *got.inReplyTo != core::AnnotationId::ofObject(irt.value(QStringLiteral("object")).toInteger())))
        bad(QStringLiteral("inReplyTo"));
    const bool group = want.value(QStringLiteral("replyType")).toString() == QLatin1String("Group");
    if ((got.replyType == core::ReplyType::Group) != group) bad(QStringLiteral("replyType"));

    auto object = [&](const char* key, const std::optional<core::AnnotationId>& ours) {
        const QJsonValue value = want.value(QLatin1String(key));
        if (value.isUndefined() ? ours.has_value()
                                : (!ours || *ours != core::AnnotationId::ofObject(value.toInteger())))
            bad(QLatin1String(key));
    };
    object("popup", got.popup);
    object("parent", got.parent);
    if (want.contains(QStringLiteral("open")) != got.open.has_value()
        || (got.open && *got.open != want.value(QStringLiteral("open")).toBool()))
        bad(QStringLiteral("open"));
    if (want.value(QStringLiteral("rotate")).toInt() != got.rotate) bad(QStringLiteral("rotate"));
    if (want.value(QStringLiteral("hasAppearance")).toBool() != got.hasAppearance) bad(QStringLiteral("hasAppearance"));
}

// Every page of `wanted` (a label's pages, possibly a subset) against the store. Returns problems.
QStringList compareLabel(const QJsonObject& wanted, const core::AnnotationStore& store)
{
    QStringList problems;
    std::set<std::size_t> described;
    for (const QJsonValue pageValue : wanted.value(QStringLiteral("pages")).toArray()) {
        const QJsonObject page = pageValue.toObject();
        const int index = page.value(QStringLiteral("index")).toInt();
        const QJsonArray entries = page.value(QStringLiteral("annots")).toArray();
        const std::vector<core::AnnotationListingEntry>& listing = store.listing(index);
        if (static_cast<qsizetype>(listing.size()) > entries.size())
            problems << QStringLiteral("page %1: %2 annotation(s) INVENTED -- the store lists %3, the label %4")
                            .arg(index + 1).arg(listing.size() - entries.size()).arg(listing.size()).arg(entries.size());
        if (static_cast<qsizetype>(listing.size()) < entries.size())
            problems << QStringLiteral("page %1: %2 annotation(s) LOST").arg(index + 1).arg(entries.size() - listing.size());
        for (qsizetype k = 0; k < std::min<qsizetype>(entries.size(), listing.size()); ++k) {
            const QJsonObject want = entries[k].toObject();
            const QString where = QStringLiteral("page %1 entry %2").arg(index + 1).arg(k);
            const core::AnnotationListingEntry& entry = listing[static_cast<std::size_t>(k)];
            if (!entry.annotation) { problems << where + QStringLiteral(": no annotation"); continue; }
            const core::Annotation& got = store.at(*entry.annotation);
            const QJsonValue object = want.value(QStringLiteral("object"));
            const core::AnnotationId id = object.isNull() ? core::AnnotationId::ofDirect(index, static_cast<int>(k))
                                                          : core::AnnotationId::ofObject(object.toInteger());
            if (got.id != id) problems << where + QStringLiteral(": identity ") + got.id.toString();
            compareEntry(want, got, where, problems);
            described.insert(*entry.annotation);
        }
    }
    return problems;
}

core::DocumentHandle openOrFail(pdf::DocumentBackendImpl& backend, const QString& relative,
                                const core::PasswordCallback& password = nullptr)
{
    auto opened = backend.open(corpusFile(relative), password);
    INFO(relative.toStdString());
    REQUIRE(opened);
    return opened.value();
}

const QStringList kLabelled = {
    QStringLiteral("annots-every-type"), QStringLiteral("annots-no-ap"), QStringLiteral("annots-threads"),
    QStringLiteral("annots-flags"), QStringLiteral("annots-rotated"), QStringLiteral("annots-direct"),
    QStringLiteral("annots-unknown-keys"), QStringLiteral("annots-900"), QStringLiteral("annots-attachments"),
    QStringLiteral("annots-measure-scale"), QStringLiteral("annots-report-v1"),
};

} // namespace

// A-17 -------------------------------------------------------------------------------------------

TEST_CASE("the reader returns every labelled annotation field by field", "[annotreader][A-17]")
{
    pdf::DocumentBackendImpl backend;
    int files = 0;
    int entries = 0;
    for (const QString& name : kLabelled) {
        const QJsonObject wanted = label(QStringLiteral("labels/") + name + QStringLiteral(".json"));
        const QString file = wanted.value(QStringLiteral("file")).toString();
        const core::DocumentHandle handle = openOrFail(backend, file);
        const core::AnnotationStore* store = backend.annotations(handle);
        REQUIRE(store != nullptr);
        REQUIRE(store->status());
        const QStringList problems = compareLabel(wanted, *store);
        INFO(file.toStdString() << "\n" << problems.join(QLatin1Char('\n')).toStdString());
        CHECK(problems.isEmpty());
        ++files;
        for (const QJsonValue page : wanted.value(QStringLiteral("pages")).toArray())
            entries += static_cast<int>(page.toObject().value(QStringLiteral("annots")).toArray().size());
        REQUIRE(backend.close(handle));
    }
    std::printf("A-17: %d labelled files, %d listed annotations compared field by field\n", files, entries);
    CHECK(files == 11);
}

TEST_CASE("a label entry removed is reported as an invented annotation", "[annotreader][A-17]")
{
    // THE CONTROL: the same comparison over a label with one entry taken away must FAIL, and fail by
    // naming an invented annotation -- a comparison that only walked the label would pass it.
    pdf::DocumentBackendImpl backend;
    QJsonObject wanted = label(QStringLiteral("labels/annots-every-type.json"));
    const core::DocumentHandle handle = openOrFail(backend, wanted.value(QStringLiteral("file")).toString());
    QJsonArray pages = wanted.value(QStringLiteral("pages")).toArray();
    QJsonObject first = pages[0].toObject();
    QJsonArray annots = first.value(QStringLiteral("annots")).toArray();
    annots.removeLast();
    first.insert(QStringLiteral("annots"), annots);
    pages[0] = first;
    wanted.insert(QStringLiteral("pages"), pages);
    const QStringList problems = compareLabel(wanted, *backend.annotations(handle));
    REQUIRE_FALSE(problems.isEmpty());
    CHECK(problems.first().contains(QStringLiteral("INVENTED")));
    REQUIRE(backend.close(handle));
}

TEST_CASE("an annotation several pages list is one annotation owned by the page its P names", "[annotreader][A-17]")
{
    pdf::DocumentBackendImpl backend;
    const core::DocumentHandle handle = openOrFail(backend, QStringLiteral("ordinary/annots-direct.pdf"));
    const core::AnnotationStore& store = *backend.annotations(handle);
    const QJsonObject wanted = label(QStringLiteral("labels/annots-direct.json"));
    long long shared = -1;
    for (const QJsonValue entry : wanted.value(QStringLiteral("pages")).toArray()[0].toObject()
                                       .value(QStringLiteral("annots")).toArray()) {
        if (entry.toObject().contains(QStringLiteral("listedBy"))) shared = entry.toObject().value(QStringLiteral("object")).toInteger();
    }
    REQUIRE(shared > 0);
    const std::optional<std::size_t> index = store.findObject(shared);
    REQUIRE(index);
    CHECK(store.at(*index).page == 0);
    int listings = 0;
    for (int page = 0; page < store.pageCount(); ++page) {
        for (const core::AnnotationListingEntry& entry : store.listing(page)) listings += entry.annotation == index ? 1 : 0;
    }
    CHECK(listings == 2);
    const std::vector<std::size_t> ownedBySecond = store.onPage(1);
    CHECK(std::count(ownedBySecond.begin(), ownedBySecond.end(), *index) == 0);
    REQUIRE(backend.close(handle));
}

// A-18 -------------------------------------------------------------------------------------------

TEST_CASE("the engine and the first-party reader agree on every page of the corpus", "[annotreader][A-18]")
{
    pdf::DocumentBackendImpl backend;
    int files = 0;
    int pages = 0;
    int entries = 0;
    QStringList disagreements;
    QDirIterator walk(QStringLiteral(RAWPAGE_CORPUS_DIR), {QStringLiteral("*.pdf")}, QDir::Files,
                      QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        const QString path = walk.next();
        auto opened = backend.open(path, [](int) { return std::optional<QString>(QStringLiteral("user")); });
        if (!opened) continue;                                   // the engine refuses it: nothing to agree on
        const core::DocumentHandle handle = opened.value();
        const core::AnnotationStore* store = backend.annotations(handle);
        const core::Permissions* permissions = backend.permissions(handle);
        REQUIRE(store != nullptr);
        REQUIRE(permissions != nullptr);
        if (store->status()) {
            ++files;
            pages += store->pageCount();
            for (int page = 0; page < store->pageCount(); ++page) entries += static_cast<int>(store->listing(page).size());
            for (const int page : permissions->annotationDisagreements())
                disagreements << QStringLiteral("%1 page %2").arg(QDir(QStringLiteral(RAWPAGE_CORPUS_DIR)).relativeFilePath(path)).arg(page + 1);
        }
        REQUIRE(backend.close(handle));
    }
    std::printf("A-18: %d files both readers read, %d pages, %d /Annots entries; %d page(s) disagree%s\n",
                files, pages, entries, static_cast<int>(disagreements.size()),
                disagreements.isEmpty() ? "" : (": " + disagreements.join(QStringLiteral(", ")).toStdString()).c_str());
    CHECK(files >= 90);
    CHECK(entries >= 9000);
    CHECK(disagreements.isEmpty());
}

TEST_CASE("a page the readers disagree on is read-only and the others are not", "[annotreader][A-18]")
{
    // THE PLANT: the first-party reading of page 2 loses one entry -- the reader "told to skip" it -- and
    // the engine's facts are the engine's own, from a second open of the same file.
    const QString path = corpusFile(QStringLiteral("ordinary/annots-threads.pdf"));
    pdf::DocumentBackendImpl backend;
    const core::DocumentHandle handle = openOrFail(backend, QStringLiteral("ordinary/annots-threads.pdf"));
    auto engine = pdf::render::PdfiumDocument::open(path, nullptr);
    REQUIRE(engine);
    const std::vector<std::vector<core::AnnotationFact>>& theirs = engine.value()->annotationFacts();
    std::vector<std::vector<core::AnnotationFact>> ours = backend.annotations(handle)->listingFacts();
    REQUIRE(ours.size() == 2);
    REQUIRE(ours[1].size() >= 2);

    core::Permissions unplanted = *backend.permissions(handle);
    CHECK(unplanted.mayAnnotate(0));
    CHECK(unplanted.mayAnnotate(1));

    ours[1].erase(ours[1].begin());
    core::Permissions planted = *backend.permissions(handle);
    planted.assessAnnotations(theirs, ours);
    CHECK(planted.mayAnnotate(0));
    const core::Status refused = planted.mayAnnotate(1);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.edit.annotationsDisagree"));
    CHECK(refused.error().context == QStringLiteral("2"));
    CHECK(planted.may(core::Operation::EditPages));            // the PAGES stay editable: only comments are refused
    REQUIRE(backend.close(handle));
}

TEST_CASE("the readers agree on awkward listing entries entry by entry", "[annotreader][A-18]")
{
    // What the corpus does not carry and the agreement rests on: how the ENGINE reads an /Annots entry
    // the first-party reader was written to read the same way -- no /Rect, a /Rect of five numbers, a
    // reversed one, a name inside one, an indirect /Rect, a reference to no object, a direct
    // dictionary, an object that is not a dictionary, an unknown subtype and no subtype at all. Built in
    // memory (`tests/unit/TinyPdf.h`) and opened by the shipped backend from a scratch file.
    const std::string bytes = tests::annotatedDocument(1, [](int, tests::TinyPdf& pdf, int page) {
        const std::string p = " /P " + std::to_string(page) + " 0 R";
        const int rectArray = pdf.add("[ 5 6 50 60 ]");
        std::vector<std::string> entries;
        auto indirect = [&](const std::string& body) { entries.push_back(std::to_string(pdf.add(body)) + " 0 R"); };
        indirect("<< /Type /Annot /Subtype /Square" + p + " >>");
        indirect("<< /Type /Annot /Subtype /Circle /Rect [ 0 0 10 10 20 ]" + p + " >>");
        indirect("<< /Type /Annot /Subtype /Square /Rect [ 300 680 60 600 ]" + p + " >>");
        indirect("<< /Type /Annot /Subtype /Line /Rect [ 0 /Foo 10 10 ]" + p + " >>");
        indirect("<< /Type /Annot /Subtype /Ink /Rect " + std::to_string(rectArray) + " 0 R" + p + " >>");
        entries.push_back("999 0 R");
        entries.push_back("<< /Type /Annot /Subtype /Text /Rect [ 1 2 3 4 ] >>");
        indirect("42");
        indirect("<< /Type /Annot /Subtype /Projection /Rect [ 0 0 5 5 ]" + p + " >>");
        indirect("<< /Type /Annot /Rect [ 0 0 5 5 ]" + p + " >>");
        std::string out = "[ ";
        for (const std::string& entry : entries) out += entry + " ";
        return out + "]";
    });
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString path = scratch.filePath(QStringLiteral("awkward.pdf"));
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size()));
    }
    auto engine = pdf::render::PdfiumDocument::open(path, nullptr);
    REQUIRE(engine);
    const auto reading = tests::readAnnotations(bytes);
    REQUIRE(reading);
    const std::vector<core::AnnotationFact>& theirs = engine.value()->annotationFacts().at(0);
    const std::vector<core::AnnotationListingEntry>& ours = reading->pages.at(0);
    REQUIRE(theirs.size() == 10);
    REQUIRE(ours.size() == theirs.size());
    for (std::size_t k = 0; k < theirs.size(); ++k) {
        const core::AnnotationFact& a = theirs[k];
        const core::AnnotationFact& b = ours[k].fact;
        auto rect = [](const std::optional<core::PdfRect>& r) {
            return r ? QStringLiteral("[%1 %2 %3 %4]").arg(r->left()).arg(r->bottom()).arg(r->right()).arg(r->top())
                     : QStringLiteral("none");
        };
        INFO("entry " << k << ": engine " << a.dictionary << "/" << static_cast<int>(a.subtype) << "/"
                      << rect(a.rect).toStdString() << ", ours " << b.dictionary << "/"
                      << static_cast<int>(b.subtype) << "/" << rect(b.rect).toStdString());
        CHECK(a.dictionary == b.dictionary);
        CHECK(a.subtype == b.subtype);
        CHECK(rect(a.rect) == rect(b.rect));
    }
    core::Permissions permissions = core::Permissions::assess(engine.value()->pages(), core::CosReading::Read,
                                                              {core::CosPageFacts{0, core::PdfSize{612, 792}}});
    permissions.assessAnnotations(engine.value()->annotationFacts(),
                                  core::AnnotationStore::fromReading(*reading).listingFacts());
    CHECK(permissions.annotationDisagreements().empty());
}

TEST_CASE("a page the two readers read differently opens read-only through the backend", "[annotreader][A-18]")
{
    // The backend's own wiring of D6-3, which no corpus file can show because on every corpus page the
    // readers agree: a coordinate too long for a double, which `pdf/cos/Lexer` CLAMPS to a finite value
    // and the engine reads its own way. Page 2 carries it; page 1 is the control.
    const std::string huge = "1" + std::string(400, '0') + ".5";
    const std::string bytes = tests::annotatedDocument(2, [&](int page, tests::TinyPdf& pdf, int object) {
        const std::string right = page == 1 ? huge : std::string("100");
        return "[ " + std::to_string(pdf.add("<< /Type /Annot /Subtype /Square /Rect [ 0 0 " + right
                                             + " 50 ] /P " + std::to_string(object) + " 0 R >>")) + " 0 R ]";
    });
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString path = scratch.filePath(QStringLiteral("disagree.pdf"));
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size()));
    }
    pdf::DocumentBackendImpl backend;
    auto opened = backend.open(path, nullptr);
    REQUIRE(opened);
    const core::Permissions& permissions = *backend.permissions(opened.value());
    const core::AnnotationFact theirs = pdf::render::PdfiumDocument::open(path, nullptr).value()->annotationFacts().at(1).at(0);
    const core::AnnotationFact ours = backend.annotations(opened.value())->listing(1).at(0).fact;
    INFO("engine right edge " << (theirs.rect ? theirs.rect->right() : -1.0) << ", ours "
                              << (ours.rect ? ours.rect->right() : -1.0));
    CHECK(permissions.mayAnnotate(0));
    CHECK(permissions.annotationDisagreements() == std::vector<int>{1});
    CHECK_FALSE(permissions.mayAnnotate(1));
    REQUIRE(backend.close(opened.value()));
}

// A-19 -------------------------------------------------------------------------------------------

TEST_CASE("an encrypted document's annotations are refused by key", "[annotreader][A-19]")
{
    pdf::DocumentBackendImpl backend;
    int refused = 0;
    for (const QString& name : QDir(corpusFile(QStringLiteral("encrypted"))).entryList({QStringLiteral("*.pdf")})) {
        const core::DocumentHandle handle = openOrFail(backend, QStringLiteral("encrypted/") + name,
                                                       [](int) { return std::optional<QString>(QStringLiteral("user")); });
        const core::AnnotationStore* store = backend.annotations(handle);
        REQUIRE(store != nullptr);
        INFO(name.toStdString());
        const core::Status status = store->status();
        REQUIRE_FALSE(status);
        CHECK(status.error().messageKey == QLatin1String("error.edit.encrypted"));
        CHECK(store->size() == 0);
        CHECK_FALSE(backend.permissions(handle)->may(core::Operation::Annotate));
        CHECK_FALSE(backend.permissions(handle)->mayAnnotate(0));
        ++refused;
        REQUIRE(backend.close(handle));
    }
    CHECK(refused == 5);
}

TEST_CASE("an annotation no page lists is reported in the log and absent from the store", "[annotreader][A-19]")
{
    QStringList lines;
    core::setLogSink([&lines](const core::LogRecordData& record) { lines << record.message; });
    pdf::DocumentBackendImpl backend;
    const core::DocumentHandle handle = openOrFail(backend, QStringLiteral("ordinary/annots-direct.pdf"));
    core::setLogSink({});

    const QJsonArray unlisted = label(QStringLiteral("labels/annots-direct.json")).value(QStringLiteral("unlisted")).toArray();
    REQUIRE(unlisted.size() == 1);
    const long long orphan = unlisted[0].toObject().value(QStringLiteral("object")).toInteger();
    const core::AnnotationStore& store = *backend.annotations(handle);
    CHECK_FALSE(store.findObject(orphan));
    bool noted = false;
    for (const core::AnnotationNote& note : store.notes())
        noted = noted || (note.kind == core::AnnotationNoteKind::Unlisted && note.object == orphan);
    CHECK(noted);
    const QString said = QStringLiteral("object %1 is an annotation no page lists").arg(orphan);
    INFO(lines.join(QLatin1Char('\n')).toStdString());
    CHECK(std::any_of(lines.begin(), lines.end(), [&said](const QString& line) { return line.contains(said); }));
    // The state reply that points at it has no parent in the store, and says so.
    bool missing = false;
    for (const core::AnnotationNote& note : store.notes())
        missing = missing || note.kind == core::AnnotationNoteKind::MissingParent;
    CHECK(missing);
    REQUIRE(backend.close(handle));
}
