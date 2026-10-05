// tst_annotstore -- Stage 6 T2, guide cases A-21 and A-22 (`Stage6-Annotations-Testing-Guide.md` §5.4),
// and the replay of `fuzz_annot_pipeline`'s body (A-23's Windows half). Headless: the store is read from
// the corpus through `pdf/ops/AnnotationReader`, the same walk the backend makes, with no engine linked.
//
//   * **A-21.** `annots-threads.pdf`'s threads, groups, review and marked histories and its three
//     broken-thread controls, each graded against `labels/annots-threads.json` -- the generator's record,
//     never a second reading.
//   * **A-22.** Hostile input, built in memory (`tests/unit/TinyPdf.h`): non-finite rectangles, a
//     1,000,000-point `/InkList`, a 10,000-deep `/IRT` chain and a 10,000-member cycle, `/Annots` that is
//     a dictionary, one annotation listed by 1,000 pages. Each read and threaded within ONE SECOND, the
//     defect reported, nothing kept unbounded.
//   * **The fuzz body replayed** over every corpus PDF: its invariants hold on every file the sink walks.
//     Known files find nothing new by construction (convention 19) -- this proves the body runs and
//     agrees with the corpus, not that the pipeline is safe; the fuzzer's minutes on Linux are that claim.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/AnnotationStore.h"
#include "pdf/ops/AnnotationReader.h"
#include "tests/fuzz/AnnotPipelineFuzzBody.h"
#include "tests/unit/TinyPdf.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>

using namespace rawpage;
using core::AnnotationNoteKind;
using core::AnnotationStore;

namespace {

QByteArray corpusBytes(const QString& relative)
{
    QFile file(QStringLiteral(RAWPAGE_CORPUS_DIR "/") + relative);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

QJsonObject label(const QString& relative)
{
    return QJsonDocument::fromJson(corpusBytes(relative)).object();
}

AnnotationStore storeOf(const QString& relative)
{
    const auto reading = tests::readAnnotations(corpusBytes(relative).toStdString());
    REQUIRE(reading);
    return AnnotationStore::fromReading(*reading);
}

std::size_t indexOf(const AnnotationStore& store, long long object)
{
    const auto found = store.findObject(object);
    REQUIRE(found);
    return *found;
}

long long objectOf(const AnnotationStore& store, std::size_t index) { return store.at(index).id.object; }

bool noted(const AnnotationStore& store, AnnotationNoteKind kind, long long object)
{
    return std::any_of(store.notes().begin(), store.notes().end(), [&](const core::AnnotationNote& note) {
        return note.kind == kind && note.object == object;
    });
}

std::set<long long> objects(const QJsonArray& array)
{
    std::set<long long> out;
    for (const QJsonValue value : array) out.insert(value.toInteger());
    return out;
}

} // namespace

// A-21 -------------------------------------------------------------------------------------------

TEST_CASE("every reply hangs under the parent the label names", "[annotstore][A-21]")
{
    const AnnotationStore store = storeOf(QStringLiteral("ordinary/annots-threads.pdf"));
    const QJsonObject wanted = label(QStringLiteral("labels/annots-threads.json"));
    const QJsonObject replies = wanted.value(QStringLiteral("replies")).toObject();
    REQUIRE(replies.size() >= 3);
    for (auto it = replies.begin(); it != replies.end(); ++it) {
        const std::size_t parent = indexOf(store, it.key().toLongLong());
        std::set<long long> got;
        for (const std::size_t child : store.replies(parent)) got.insert(objectOf(store, child));
        // The label lists every /RT /R child; the store under a parent keeps only the ones whose thread is
        // whole -- a child on another page is top-level (D6-12), so it is subtracted here, and checked below.
        // ...and the members of the /IRT cycle, which the label lists as each other's children and the
        // store, having broken the cycle, shows at the top.
        std::set<long long> want = objects(it.value().toArray());
        const QJsonObject controls = wanted.value(QStringLiteral("controls")).toObject();
        want.erase(controls.value(QStringLiteral("crossPage")).toInteger());
        for (const long long member : objects(controls.value(QStringLiteral("cycle")).toArray())) want.erase(member);
        INFO("parent " << it.key().toStdString());
        CHECK(got == want);
    }
    // A reply to a reply: the second level is under the first, not under the root.
    bool nested = false;
    for (const std::size_t index : store.topLevel()) {
        for (const std::size_t child : store.replies(index)) nested = nested || !store.replies(child).empty();
    }
    CHECK(nested);
}

TEST_CASE("a group is one item with its parent", "[annotstore][A-21]")
{
    const AnnotationStore store = storeOf(QStringLiteral("ordinary/annots-threads.pdf"));
    const QJsonObject group = label(QStringLiteral("labels/annots-threads.json"))
                                  .value(QStringLiteral("groups")).toArray()[0].toObject();
    const std::size_t caret = indexOf(store, group.value(QStringLiteral("parent")).toInteger());
    std::set<long long> members;
    for (const std::size_t member : store.groupMembers(caret)) {
        members.insert(objectOf(store, member));
        CHECK_FALSE(store.isTopLevel(member));
    }
    CHECK(members == objects(group.value(QStringLiteral("members")).toArray()));
    CHECK(store.isTopLevel(caret));
    CHECK(store.replies(caret).empty());       // a group member is not a comment under it
}

TEST_CASE("review and marked histories are the label's in time order", "[annotstore][A-21]")
{
    const AnnotationStore store = storeOf(QStringLiteral("ordinary/annots-threads.pdf"));
    const QJsonObject wanted = label(QStringLiteral("labels/annots-threads.json"));
    int histories = 0;
    for (const char* model : {"reviewHistory", "markedHistory"}) {
        const QJsonObject byParent = wanted.value(QLatin1String(model)).toObject();
        for (auto it = byParent.begin(); it != byParent.end(); ++it) {
            const std::size_t parent = indexOf(store, it.key().toLongLong());
            const std::vector<core::AnnotationStateChange> got =
                QLatin1String(model) == QLatin1String("reviewHistory") ? store.reviewHistory(parent)
                                                                       : store.markedHistory(parent);
            const QJsonArray want = it.value().toArray();
            INFO(model << " on " << it.key().toStdString());
            REQUIRE(static_cast<qsizetype>(got.size()) == want.size());
            for (qsizetype k = 0; k < want.size(); ++k) {
                const QJsonObject w = want[k].toObject();
                const core::AnnotationStateChange& g = got[static_cast<std::size_t>(k)];
                CHECK(g.author == w.value(QStringLiteral("author")).toString());
                CHECK(g.state == w.value(QStringLiteral("state")).toString());
                CHECK(g.date.utcIso() == w.value(QStringLiteral("createdUtc")).toString());
                CHECK(objectOf(store, g.reply) == w.value(QStringLiteral("object")).toInteger());
            }
            ++histories;
        }
    }
    CHECK(histories == 4);

    // The current review status is the LATEST, not the first and not the most recently written -- on
    // every annotation with a history, at least one of which has more than one entry (else first and
    // latest coincide and this would hold of either rule; found by T2's planted control P3).
    const QJsonObject reviews = wanted.value(QStringLiteral("reviewHistory")).toObject();
    bool longHistory = false;
    for (auto it = reviews.begin(); it != reviews.end(); ++it) {
        const QJsonArray history = it.value().toArray();
        longHistory = longHistory || history.size() > 1;
        const auto current = store.currentReview(indexOf(store, it.key().toLongLong()));
        REQUIRE(current);
        CHECK(current->state == history.last().toObject().value(QStringLiteral("state")).toString());
    }
    CHECK(longHistory);
}

TEST_CASE("a checkmark is per author", "[annotstore][A-21]")
{
    const AnnotationStore store = storeOf(QStringLiteral("ordinary/annots-threads.pdf"));
    const QJsonObject marked = label(QStringLiteral("labels/annots-threads.json")).value(QStringLiteral("markedHistory")).toObject();
    int checked = 0;
    for (auto it = marked.begin(); it != marked.end(); ++it) {
        const std::size_t parent = indexOf(store, it.key().toLongLong());
        std::map<QString, QString> latest;
        for (const QJsonValue change : it.value().toArray())
            latest[change.toObject().value(QStringLiteral("author")).toString()] = change.toObject().value(QStringLiteral("state")).toString();
        for (const auto& [author, state] : latest) {
            INFO(author.toStdString() << " on " << it.key().toStdString());
            CHECK(store.isMarkedBy(parent, author) == (state == QLatin1String("Marked")));
            ++checked;
        }
        // Someone who never marked it has not marked it.
        CHECK_FALSE(store.isMarkedBy(parent, QStringLiteral("Ben Okafor")));
    }
    CHECK(checked == 2);
}

TEST_CASE("a cycle an orphan and a cross-page reply are top-level and reported", "[annotstore][A-21]")
{
    const AnnotationStore store = storeOf(QStringLiteral("ordinary/annots-threads.pdf"));
    const QJsonObject wanted = label(QStringLiteral("labels/annots-threads.json"));
    const QJsonObject controls = wanted.value(QStringLiteral("controls")).toObject();
    for (const QJsonValue member : controls.value(QStringLiteral("cycle")).toArray()) {
        const std::size_t index = indexOf(store, member.toInteger());
        CHECK(store.isTopLevel(index));
        CHECK(noted(store, AnnotationNoteKind::ReplyCycle, member.toInteger()));
    }
    for (const QJsonValue orphan : controls.value(QStringLiteral("missingParent")).toArray()) {
        CHECK(store.isTopLevel(indexOf(store, orphan.toInteger())));
        CHECK(noted(store, AnnotationNoteKind::MissingParent, orphan.toInteger()));
    }
    const long long across = controls.value(QStringLiteral("crossPage")).toInteger();
    CHECK(store.isTopLevel(indexOf(store, across)));
    CHECK(noted(store, AnnotationNoteKind::ReplyOnOtherPage, across));

    // The whole top level: the label's roots, plus every annotation of the three controls.
    std::set<long long> want = objects(wanted.value(QStringLiteral("topLevel")).toArray());
    for (const long long n : objects(controls.value(QStringLiteral("cycle")).toArray())) want.insert(n);
    for (const long long n : objects(controls.value(QStringLiteral("missingParent")).toArray())) want.insert(n);
    want.insert(across);
    std::set<long long> got;
    for (const std::size_t index : store.topLevel()) got.insert(objectOf(store, index));
    CHECK(got == want);
    CHECK(store.authors().size() == 3);
}

TEST_CASE("queries filter and sort the top level", "[annotstore][A-21]")
{
    const AnnotationStore threads = storeOf(QStringLiteral("ordinary/annots-threads.pdf"));
    AnnotationStore::Filter byAuthor;
    byAuthor.authors = {QStringLiteral("Ben Okafor")};
    for (const std::size_t index : threads.query(byAuthor, AnnotationStore::SortKey::Page))
        CHECK(threads.at(index).author == QStringLiteral("Ben Okafor"));
    CHECK_FALSE(threads.query(byAuthor, AnnotationStore::SortKey::Page).empty());

    AnnotationStore::Filter byText;
    byText.text = QStringLiteral("FIGURE CURRENT");           // case-insensitive
    CHECK(threads.query(byText, AnnotationStore::SortKey::Page).size() == 1);

    const std::vector<std::size_t> byDate = threads.query({}, AnnotationStore::SortKey::Date);
    for (std::size_t k = 1; k < byDate.size(); ++k) {
        const auto& before = threads.at(byDate[k - 1]);
        const auto& after = threads.at(byDate[k]);
        const auto t0 = before.modified.utcSeconds.value_or(before.created.utcSeconds.value_or(0));
        const auto t1 = after.modified.utcSeconds.value_or(after.created.utcSeconds.value_or(0));
        CHECK(t0 <= t1);
    }
    AnnotationStore::Filter secondPage;
    secondPage.pages = {1};
    for (const std::size_t index : threads.query(secondPage, AnnotationStore::SortKey::Author))
        CHECK(threads.at(index).page == 1);

    // Hidden and NoView leave the list when asked to (D6-27) -- `annots-flags.pdf` has one of each.
    const AnnotationStore flags = storeOf(QStringLiteral("ordinary/annots-flags.pdf"));
    AnnotationStore::Filter visible;
    visible.includeHidden = false;
    CHECK(flags.query({}, AnnotationStore::SortKey::Page).size()
          == flags.query(visible, AnnotationStore::SortKey::Page).size() + 2);
}

// A-22 -------------------------------------------------------------------------------------------

namespace {

// Reads `bytes` and threads the result, timed; requires the whole of it inside one second.
AnnotationStore readWithinASecond(const std::string& bytes, const char* what)
{
    QElapsedTimer clock;
    clock.start();
    const auto reading = tests::readAnnotations(bytes);
    REQUIRE(reading);
    AnnotationStore store = AnnotationStore::fromReading(*reading);
    const qint64 ms = clock.elapsed();
    std::printf("A-22 %s: %lld ms, %zu annotation(s), %zu note(s)\n", what, static_cast<long long>(ms),
                store.size(), store.notes().size());
    // THE ONE-SECOND BOUND IS THE SHIPPED BUILD'S, graded where NDEBUG is defined (`x64-release`). An
    // unoptimized `x64-debug` parses the million-point case in ~1.9 s (measured at T2), so there the case
    // proves the read ENDS and stays bounded, under a ceiling five times looser -- it does not grade speed.
#ifdef NDEBUG
    CHECK(ms < 1000);
#else
    CHECK(ms < 5000);
#endif
    return store;
}

bool hasNote(const AnnotationStore& store, AnnotationNoteKind kind)
{
    return std::any_of(store.notes().begin(), store.notes().end(),
                       [kind](const core::AnnotationNote& note) { return note.kind == kind; });
}

} // namespace

TEST_CASE("a rectangle beyond any double is read clamped and finite", "[annotstore][A-22]")
{
    // A number too long for a double. PDF syntax has no NaN and no infinity, and `pdf/cos/Lexer` CLAMPS an
    // out-of-range number (Stage 2's choice: a hostile run of digits yields a value, never garbage) -- so
    // no coordinate the first-party reader meets is non-finite, and `AnnotationNoteKind::NonFiniteRect` is
    // a defensive branch this input cannot reach. What this case holds is that the clamp reaches the
    // model: a finite rectangle, finite ink, nothing unbounded. The model's own refusal of a non-finite
    // corner is `tst_annotation`'s "a rectangle is normalized" case.
    const std::string huge = "1" + std::string(400, '0') + ".5";
    const AnnotationStore store = readWithinASecond(tests::annotatedDocument(1, [&](int, tests::TinyPdf& pdf, int) {
        const int a = pdf.add("<< /Type /Annot /Subtype /Square /Rect [ 0 0 " + huge + " 10 ] >>");
        const int b = pdf.add("<< /Type /Annot /Subtype /Ink /Rect [ 0 0 10 10 ] /InkList [ [ 1 2 " + huge + " 4 5 6 ] ] >>");
        return "[ " + std::to_string(a) + " 0 R " + std::to_string(b) + " 0 R ]";
    }), "non-finite rect");
    REQUIRE(store.size() == 2);
    CHECK(store.at(0).rectFinite);
    CHECK(std::isfinite(store.at(0).rect.right()));
    CHECK(store.at(0).rect.right() > 1e15);                   // clamped high, not wrapped or zeroed
    CHECK_FALSE(hasNote(store, AnnotationNoteKind::NonFiniteRect));
    REQUIRE(store.at(1).inkList.size() == 1);
    CHECK(store.at(1).inkList[0].size() == 6);
    for (const double sample : store.at(1).inkList[0]) CHECK(std::isfinite(sample));
}

TEST_CASE("a million-point ink list is truncated and reported", "[annotstore][A-22]")
{
    std::string ink;
    ink.reserve(8000000);
    for (int i = 0; i < 1000000; ++i) ink += std::to_string(i % 600) + ' ' + std::to_string(i % 800) + ' ';
    const AnnotationStore store = readWithinASecond(tests::annotatedDocument(1, [&](int, tests::TinyPdf& pdf, int) {
        return "[ " + std::to_string(pdf.add("<< /Type /Annot /Subtype /Ink /Rect [ 0 0 600 800 ] /InkList [ [ " + ink + "] ] >>"))
               + " 0 R ]";
    }), "1,000,000-point /InkList");
    REQUIRE(store.size() == 1);
    REQUIRE(store.at(0).inkList.size() == 1);
    CHECK(store.at(0).inkList[0].size() == pdf::ops::AnnotationReader::kMaxCoordinates);
    CHECK(hasNote(store, AnnotationNoteKind::GeometryTruncated));
}

TEST_CASE("a ten-thousand-deep reply chain and a ten-thousand-member cycle thread in time", "[annotstore][A-22]")
{
    constexpr int kDepth = 10000;
    // Annotation k replies to annotation k-1; the cycle's first member replies to its last.
    auto chain = [](bool cycle) {
        return tests::annotatedDocument(1, [cycle](int, tests::TinyPdf& pdf, int) {
            const int first = pdf.reserve() ;
            std::string refs = "[ ";
            std::vector<int> numbers{first};
            for (int k = 1; k < kDepth; ++k) numbers.push_back(pdf.reserve());
            for (int k = 0; k < kDepth; ++k) {
                std::string body = "<< /Type /Annot /Subtype /Text /Rect [ 0 0 20 20 ]";
                if (k > 0) body += " /IRT " + std::to_string(numbers[static_cast<std::size_t>(k - 1)]) + " 0 R";
                else if (cycle) body += " /IRT " + std::to_string(numbers.back()) + " 0 R";
                pdf.set(numbers[static_cast<std::size_t>(k)], body + " >>");
                refs += std::to_string(numbers[static_cast<std::size_t>(k)]) + " 0 R ";
            }
            return refs + "]";
        });
    };
    const AnnotationStore deep = readWithinASecond(chain(false), "10,000-deep /IRT chain");
    REQUIRE(deep.size() == kDepth);
    CHECK(deep.topLevel().size() == 1);
    CHECK(deep.replies(0).size() == 1);
    std::size_t depth = 0;
    for (std::optional<std::size_t> at = deep.size() - 1; at; at = deep.parentOf(*at)) ++depth;
    CHECK(depth == kDepth);

    const AnnotationStore loop = readWithinASecond(chain(true), "10,000-member /IRT cycle");
    CHECK(loop.topLevel().size() == static_cast<std::size_t>(kDepth));       // every member, reported
    CHECK(std::count_if(loop.notes().begin(), loop.notes().end(), [](const core::AnnotationNote& note) {
              return note.kind == AnnotationNoteKind::ReplyCycle;
          }) == kDepth);
}

TEST_CASE("an Annots dictionary lists nothing and is reported", "[annotstore][A-22]")
{
    const AnnotationStore store = readWithinASecond(tests::annotatedDocument(1, [](int, tests::TinyPdf&, int) {
        return std::string("<< /Type /Annot /Subtype /Square /Rect [ 0 0 1 1 ] >>");
    }), "/Annots dictionary");
    CHECK(store.size() == 0);
    CHECK(store.listing(0).empty());
    CHECK(hasNote(store, AnnotationNoteKind::AnnotsNotArray));
}

TEST_CASE("one annotation listed by a thousand pages is one annotation", "[annotstore][A-22]")
{
    int shared = 0;
    int owner = 0;
    const AnnotationStore store = readWithinASecond(tests::annotatedDocument(1000, [&](int page, tests::TinyPdf& pdf, int pageObject) {
        if (page == 0) shared = pdf.reserve();
        if (page == 500) {
            owner = pageObject;
            pdf.set(shared, "<< /Type /Annot /Subtype /Square /Rect [ 0 0 1 1 ] /P " + std::to_string(owner) + " 0 R >>");
        }
        return "[ " + std::to_string(shared) + " 0 R ]";
    }), "one annotation on 1,000 pages");
    REQUIRE(store.size() == 1);
    CHECK(store.at(0).page == 500);                       // owned by the page its /P names
    for (int page = 0; page < 1000; ++page) {
        REQUIRE(store.listing(page).size() == 1);
        CHECK(store.listing(page)[0].annotation == std::optional<std::size_t>(0));
    }
    CHECK(std::count_if(store.notes().begin(), store.notes().end(), [](const core::AnnotationNote& note) {
              return note.kind == AnnotationNoteKind::ListedByManyPages;
          }) == 999);
}

// The fuzz body, replayed -----------------------------------------------------------------------

TEST_CASE("the annotation fuzz body holds its invariants over every corpus file", "[annotstore][fuzz][A-23]")
{
    int files = 0;
    int read = 0;
    std::size_t annotations = 0;
    QDirIterator walk(QStringLiteral(RAWPAGE_CORPUS_DIR), {QStringLiteral("*.pdf")}, QDir::Files,
                      QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        QFile file(walk.next());
        REQUIRE(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        const fuzz::AnnotVerdict verdict = fuzz::annotPipelineFuzzOnce(
            reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size()));
        INFO(file.fileName().toStdString() << ": " << verdict.failure);
        CHECK(verdict.ok);
        ++files;
        read += verdict.read ? 1 : 0;
        annotations += verdict.annotations;
    }
    std::printf("A-23 replay: %d corpus files, %d walked, %zu annotations through the body\n", files, read, annotations);
    CHECK(files >= 100);
    CHECK(annotations > 9000);
}

TEST_CASE("the annotation fuzz body's invariants fire", "[annotstore][fuzz][A-23]")
{
    // THE CONTROL, convention 7: a body whose checks never fail passes the replay above. A store cannot be
    // made to break its invariants from bytes -- that is what a finding would be -- so the control drives
    // the JSON invariant, the one a plausible regression breaks, with an annotation whose round trip
    // loses a field once the JSON writer forgets it.
    core::Annotation a;
    a.id = core::AnnotationId::ofObject(9);
    a.subtype = core::AnnotationSubtype::Text;
    a.contents = QStringLiteral("kept");
    QJsonObject json = a.toJson();
    json.remove(QStringLiteral("contents"));                  // the writer that forgot a field
    const auto back = core::Annotation::fromJson(json);
    REQUIRE(back);
    CHECK_FALSE(back.value() == a);                         // ...is caught by the equality the body uses
}
