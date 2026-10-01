// tst_searchclasses -- the four other content classes over `corpus/ordinary/search-classes.pdf`, through the
// real engine: `pdf::DocumentBackendImpl::searchUnits` run as an engine task on the shipped `RenderScheduler`,
// kept by `core/text/DocumentIndex` and matched by `SearchEngine` -- the assembly `app/CompositionRoot`
// builds, minus the window. Stage 5 T8, decision D5-12; guide cases A-76, A-78 and A-79
// (`Stage5-TextSearch-Testing-Guide.md` §5.10).
//
// THE ORACLE IS THE GENERATOR (rule 7): which word each class carries, and which words must be found
// nowhere, is read from `corpus/labels/search-classes.json` -- `classes` and `notFound` -- which
// `corpus/make-text.py` wrote when it built the file. Nothing is read out of PDFium first.
//
// WHAT IT CANNOT CLAIM (convention 19): how a hit NAVIGATES -- a comment's page and rectangle, a bookmark's
// destination, an attachment that opens nothing -- is `tests/composition/tst_selectionink.cpp`'s A-77,
// through the find bar and the view. And every document here is ours (D5-23).

#include <catch2/catch_test_macros.hpp>

#include "app/BackendTextExtractor.h"
#include "core/backend.h"
#include "core/doc/PageTree.h"
#include "core/render/RenderScheduler.h"
#include "core/text/DocumentIndex.h"
#include "core/text/SearchEngine.h"
#include "core/text/SearchQuery.h"
#include "core/text/SearchUnit.h"
#include "pdf/DocumentBackendImpl.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

using namespace rawpage;
using core::text::ContentClass;
using core::text::DocumentIndex;
using core::text::SearchEngine;
using core::text::SearchQuery;
using core::text::SearchUnit;

namespace {

QString corpusFile(const char* relative)
{
    return QStringLiteral(RAWPAGE_CORPUS_DIR "/") + QString::fromUtf8(relative);
}

// The generator's record of the file (rule 7).
QJsonObject label()
{
    QFile file(corpusFile("labels/search-classes.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    REQUIRE(document.isObject());
    return document.object();
}

// Every engine thread the text and the units were read on, and every part the reader was asked for.
struct Recorder {
    std::mutex mutex;
    std::set<std::thread::id> textThreads;
    std::set<std::thread::id> unitThreads;
    std::map<int, int> parts;

    int total()
    {
        const std::lock_guard<std::mutex> lock(mutex);
        int sum = 0;
        for (const auto& [part, n] : parts) {
            (void)part;
            sum += n;
        }
        return sum;
    }
    int callsFor(int part)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        const auto found = parts.find(part);
        return found == parts.end() ? 0 : found->second;
    }
};

class RecordingExtractor final : public core::ITextExtractor {
public:
    RecordingExtractor(core::IDocumentBackend& backend, Recorder& recorder) : inner_(backend), recorder_(recorder) {}
    core::Status extractPage(core::DocumentHandle document, int page, core::text::PageTextModel& into) override
    {
        {
            const std::lock_guard<std::mutex> lock(recorder_.mutex);
            recorder_.textThreads.insert(std::this_thread::get_id());
        }
        return inner_.extractPage(document, page, into);
    }

private:
    app::BackendTextExtractor inner_;
    Recorder& recorder_;
};

// `tst_searchcorpus.cpp`'s `Searching`, with the units reader wired as `app/CompositionRoot` wires it --
// through a recorder, so A-79 can say what was read, how often, and on which thread.
struct Searching {
    Searching()
    {
        index.setUnitReader([this](core::DocumentHandle through, int part, std::vector<SearchUnit>& into) {
            {
                const std::lock_guard<std::mutex> lock(recorder.mutex);
                ++recorder.parts[part];
                recorder.unitThreads.insert(std::this_thread::get_id());
            }
            return backend.searchUnits(through, part, into);
        });
        auto opened = backend.open(corpusFile("ordinary/search-classes.pdf"), nullptr);
        REQUIRE(opened);
        handle = opened.value();
        const core::PageTree* pages = backend.pages(handle);
        REQUIRE(pages != nullptr);
        entry = index.attach(backend.documentId(handle), handle, pages->count());
        REQUIRE(entry != 0);
    }
    ~Searching()
    {
        index.detach(handle);
        scheduler.cancelDocument(handle);
        scheduler.waitForIdle(10000);
        (void)backend.close(handle);
    }

    SearchEngine::Outcome search(const QString& word, bool comments, bool bookmarks, bool fields, bool attachments)
    {
        SearchQuery query;
        query.pattern = word;
        query.classes.comments = comments;
        query.classes.bookmarks = bookmarks;
        query.classes.fields = fields;
        query.classes.attachments = attachments;
        std::optional<SearchEngine::Outcome> outcome;
        SearchEngine::Handlers handlers;
        handlers.finished = [&](const SearchEngine::Outcome& o) { outcome = o; };
        REQUIRE(engine.start(entry, query, handlers));
        QElapsedTimer timer;
        timer.start();
        while (!outcome && timer.elapsed() < 60000) {
            scheduler.waitForIdle(1);
            scheduler.deliverReady();
            engine.waitForIdle(1);
            engine.deliverReady();
        }
        REQUIRE(outcome.has_value());
        return *outcome;
    }

    Recorder recorder;
    pdf::DocumentBackendImpl backend;
    core::render::RenderScheduler scheduler{
        nullptr, [this] { return std::unique_ptr<core::ITextExtractor>(new RecordingExtractor(backend, recorder)); },
        nullptr, core::render::RenderScheduler::Config{}};
    DocumentIndex index{scheduler};
    SearchEngine engine{index};
    core::DocumentHandle handle;
    DocumentIndex::EntryId entry = 0;
};

ContentClass classNamed(const QString& name)
{
    if (name == QLatin1String("pageText")) return ContentClass::PageText;
    if (name == QLatin1String("comments")) return ContentClass::Comments;
    if (name == QLatin1String("bookmarks")) return ContentClass::Bookmarks;
    if (name == QLatin1String("fields")) return ContentClass::Fields;
    REQUIRE(name == QLatin1String("attachments"));
    return ContentClass::Attachments;
}

// The files in the temporary directory whose bytes begin as a PDF does -- what an attachment written to
// disk to be opened would leave behind (A-78).
QSet<QString> pdfsInTemp()
{
    QSet<QString> out;
    const QDir temp(QDir::tempPath());
    for (const QString& name : temp.entryList(QDir::Files)) {
        QFile file(temp.filePath(name));
        if (file.open(QIODevice::ReadOnly) && file.read(5) == QByteArrayLiteral("%PDF-")) out.insert(name);
    }
    return out;
}

} // namespace

// =============================================================================================
// A-76 -- each class is searched only when its switch is on
// =============================================================================================

TEST_CASE("each class's word in search-classes is found only with its switch on and the control never",
          "[searchclasses][A-76]")
{
    Searching s;
    const QJsonObject truth = label();
    const QJsonObject classes = truth.value(QStringLiteral("classes")).toObject();
    REQUIRE(classes.size() == 5);

    // EVERY WORD UNDER EVERY ONE-SWITCH SETTING, plus all off and all on: found exactly when its class is
    // included -- page text always.
    int checked = 0;
    for (auto it = classes.begin(); it != classes.end(); ++it) {
        const ContentClass expectedClass = classNamed(it.key());
        for (const QJsonValue word : it.value().toArray()) {
            for (int setting = 0; setting < 6; ++setting) {
                // 0..3: one switch on; 4: all off; 5: all on.
                const bool comments = setting == 0 || setting == 5;
                const bool bookmarks = setting == 1 || setting == 5;
                const bool fields = setting == 2 || setting == 5;
                const bool attachments = setting == 3 || setting == 5;
                core::text::SearchClasses on{comments, bookmarks, fields, attachments};
                const SearchEngine::Outcome outcome = s.search(word.toString(), comments, bookmarks, fields, attachments);
                INFO(word.toString().toStdString() << " (" << it.key().toStdString() << ") under setting " << setting);
                CHECK(outcome.hits.empty() == !on.includes(expectedClass));
                for (const auto& hit : outcome.hits) CHECK(hit.contentClass() == expectedClass);
                ++checked;
            }
        }
    }
    CHECK(checked == 9 * 6);

    // THE CONTROLS, every switch on: `wombat` is in no class and no byte of the file; `hedgehog` is the
    // CONTENT of the .txt attachment, which is searched by name and description only; `pangolin` is in the
    // PDF attached inside the attached PDF, which is one level too deep.
    const QJsonObject notFound = truth.value(QStringLiteral("notFound")).toObject();
    REQUIRE(notFound.size() == 3);
    for (auto it = notFound.begin(); it != notFound.end(); ++it) {
        INFO(it.key().toStdString() << ": " << it.value().toString().toStdString());
        CHECK(s.search(it.key(), true, true, true, true).hits.empty());
    }
}

TEST_CASE("each class's hit in search-classes names the object the generator put the word in",
          "[searchclasses][A-76][A-78]")
{
    Searching s;
    const auto only = [&](const QString& word) {
        const SearchEngine::Outcome outcome = s.search(word, true, true, true, true);
        REQUIRE(outcome.hits.size() == 1);
        REQUIRE(outcome.hits.front().unit != nullptr);
        return outcome.hits.front();
    };

    // THE COMMENT, at the generator's /Rect 500 700 520 720 on page 1 (`make-text.py`'s note).
    const auto comment = only(QStringLiteral("quokka"));
    CHECK(comment.unit->field == SearchUnit::Field::CommentContents);
    CHECK(comment.page == 0);
    REQUIRE(comment.unit->hasRect);
    CHECK(comment.unit->rect.left() == 500.0);
    CHECK(comment.unit->rect.bottom() == 700.0);
    CHECK(comment.unit->rect.right() == 520.0);
    CHECK(comment.unit->rect.top() == 720.0);

    // THE FIELD: its name and its value, both at its widget's /Rect 54 620 254 642.
    const auto name = only(QStringLiteral("marmot"));
    const auto value = only(QStringLiteral("walrus"));
    CHECK(name.unit->field == SearchUnit::Field::FieldName);
    CHECK(value.unit->field == SearchUnit::Field::FieldValue);
    CHECK(value.unit->locator == QStringLiteral("marmot"));
    CHECK(value.unit->rect.left() == 54.0);
    CHECK(value.unit->rect.top() == 642.0);

    // THE BOOKMARK: on no page, its destination page 1.
    const auto bookmark = only(QStringLiteral("zeppelin"));
    CHECK(bookmark.page == -1);
    CHECK(bookmark.unit->destination.pageIndex == 0);

    // THE ATTACHMENTS: a name, a description (first-party, `EmbeddedFileReader`) and, A-78, the text of the
    // attached PDF one level deep -- each naming its file.
    const auto attachmentName = only(QStringLiteral("lemur"));
    CHECK(attachmentName.unit->field == SearchUnit::Field::AttachmentName);
    CHECK(attachmentName.page == -1);
    const auto description = only(QStringLiteral("narwhal"));
    CHECK(description.unit->field == SearchUnit::Field::AttachmentDescription);
    CHECK(description.unit->locator == QStringLiteral("ocelot.txt"));
    const auto attachedText = only(QStringLiteral("axolotl"));
    CHECK(attachedText.unit->field == SearchUnit::Field::AttachedText);
    CHECK(attachedText.unit->locator == QStringLiteral("lemur.pdf"));
    CHECK(attachedText.unit->attachedPage == 0);
}

// =============================================================================================
// A-78 -- an attached PDF's text, one level deep, and nothing written to disk
// =============================================================================================

TEST_CASE("an attached PDF is searched one level deep from memory", "[searchclasses][A-78]")
{
    const QSet<QString> before = pdfsInTemp();
    {
        Searching s;
        CHECK(s.search(QStringLiteral("axolotl"), false, false, false, true).hits.size() == 1);
        // ONE LEVEL: the PDF attached inside it is never opened -- `pangolin` is its whole text.
        CHECK(s.search(QStringLiteral("pangolin"), false, false, false, true).hits.empty());
        // THE CONTROL for the depth: with attachments OFF the first level is not found either, so the
        // `pangolin` result above is the depth's and not a search that read no attachment at all.
        CHECK(s.search(QStringLiteral("axolotl"), false, false, false, false).hits.empty());
    }
    // NOTHING WRITTEN TO DISK TO OPEN IT: no new PDF in the temporary directory -- opened from bytes.
    const QSet<QString> after = pdfsInTemp();
    CHECK((after - before).isEmpty());
}

// =============================================================================================
// A-79 -- read once, on the engine thread, and only when needed
// =============================================================================================

TEST_CASE("search-classes' units are read once per part on the engine thread and only when a class needs them",
          "[searchclasses][A-79]")
{
    Searching s;
    // PAGE TEXT ONLY: the reader is never asked -- THE CONTROL.
    CHECK(s.search(QStringLiteral("papaya"), false, false, false, false).hits.size() == 1);
    CHECK(s.recorder.total() == 0);

    // A class that needs the page's part: one read.
    CHECK(s.search(QStringLiteral("quokka"), true, false, false, false).hits.size() == 1);
    CHECK(s.recorder.callsFor(0) == 1);
    CHECK(s.recorder.total() == 1);

    // Every class, twice: the outline's and the attachments' parts once each, and nothing read again.
    (void)s.search(QStringLiteral("lemur"), true, true, true, true);
    (void)s.search(QStringLiteral("zeppelin"), true, true, true, true);
    CHECK(s.recorder.callsFor(0) == 1);
    CHECK(s.recorder.callsFor(core::text::kOutlinePart) == 1);
    CHECK(s.recorder.callsFor(core::text::kAttachmentsPart) == 1);
    CHECK(s.recorder.total() == 3);

    // ON THE ENGINE THREAD: the one the page's text was extracted on, and not this one.
    const std::lock_guard<std::mutex> lock(s.recorder.mutex);
    REQUIRE(s.recorder.unitThreads.size() == 1);
    REQUIRE(s.recorder.textThreads.size() == 1);
    CHECK(*s.recorder.unitThreads.begin() == *s.recorder.textThreads.begin());
    CHECK(*s.recorder.unitThreads.begin() != std::this_thread::get_id());
}
