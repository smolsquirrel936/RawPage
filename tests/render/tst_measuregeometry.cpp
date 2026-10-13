// tst_measuregeometry -- what a measurement snaps to, read by the ENGINE: `pdf::DocumentBackendImpl::pageGeometry`
// over the real PDFium, and as `app/CompositionRoot` wires it -- an engine task of the shipped `RenderScheduler`,
// kept by `core/text/DocumentIndex`. Stage 6 T12, decision D6-14; guide cases A-89's engine half and A-91
// (`Stage6-Annotations-Testing-Guide.md` §5.15).
//
// THE ORACLE IS THE GENERATOR (rule 7): `corpus/labels/annots-measure-scale.json` lists the segments
// `corpus/make-annots.py` drew on page 1 -- read here, never out of PDFium first. The transformed form, the curve
// and the dense page are built in the case (`tests/unit/TinyPdf.h`), each with its figures worked by hand.
//
// WHAT IT CANNOT CLAIM (convention 19): that a DRAG lands on these points through the views at two zooms -- that is
// `tests/composition/tst_measureproduct`; and every document here is ours (D6-26).

#include <catch2/catch_test_macros.hpp>

#include "app/BackendTextExtractor.h"
#include "core/Snap.h"
#include "core/backend.h"
#include "core/doc/PageTree.h"
#include "core/render/RenderScheduler.h"
#include "core/text/DocumentIndex.h"
#include "pdf/DocumentBackendImpl.h"
#include "tests/unit/TinyPdf.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace rawpage;
using core::PageGeometry;
using core::PathSegment;
using core::PdfPoint;

namespace {

QString corpusFile(const char* relative)
{
    return QStringLiteral(RAWPAGE_CORPUS_DIR "/") + QString::fromUtf8(relative);
}

QJsonObject label()
{
    QFile file(corpusFile("labels/annots-measure-scale.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    REQUIRE(document.isObject());
    return document.object();
}

bool same(PdfPoint a, PdfPoint b)
{
    return std::abs(a.x - b.x) < 1e-3 && std::abs(a.y - b.y) < 1e-3;
}

bool holds(const PageGeometry& g, PdfPoint a, PdfPoint b)
{
    for (const PathSegment& s : g.segments()) {
        if (s.curve) continue;
        if ((same(s.a, a) && same(s.b, b)) || (same(s.a, b) && same(s.b, a))) return true;
    }
    return false;
}

std::string stream(const std::string& dictionary, const std::string& data)
{
    return "<< " + dictionary + " /Length " + std::to_string(data.size()) + " >>\nstream\n" + data + "\nendstream";
}

// One page whose content is `content`, with `resources` -- and whatever `extra` adds to the file first.
std::string onePage(const std::string& content, const std::function<std::string(tests::TinyPdf&)>& extra = {})
{
    tests::TinyPdf pdf;
    const int catalog = pdf.reserve();
    const int pages = pdf.reserve();
    const std::string resources = extra ? extra(pdf) : std::string();
    const int contents = pdf.add(stream("", content));
    const int page = pdf.add("<< /Type /Page /Parent " + std::to_string(pages) + " 0 R /MediaBox [ 0 0 612 792 ] /Contents "
                             + std::to_string(contents) + " 0 R /Resources << " + resources + " >> >>");
    pdf.set(catalog, "<< /Type /Catalog /Pages " + std::to_string(pages) + " 0 R >>");
    pdf.set(pages, "<< /Type /Pages /Kids [ " + std::to_string(page) + " 0 R ] /Count 1 >>");
    return pdf.bytes(catalog);
}

// A document open in the shipped backend -- a corpus file, or bytes built in the case and written to a directory
// the case owns.
struct Opened {
    QTemporaryDir directory;
    pdf::DocumentBackendImpl backend;
    core::DocumentHandle handle;

    void open(const QString& path)
    {
        auto opened = backend.open(path, nullptr);
        REQUIRE(opened);
        handle = opened.value();
    }
    static std::unique_ptr<Opened> corpus(const char* relative)
    {
        auto o = std::make_unique<Opened>();
        o->open(corpusFile(relative));
        return o;
    }
    static std::unique_ptr<Opened> built(const std::string& bytes)
    {
        auto o = std::make_unique<Opened>();
        REQUIRE(o->directory.isValid());
        const QString path = o->directory.filePath(QStringLiteral("built.pdf"));
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(bytes.data(), static_cast<qint64>(bytes.size()));
        file.close();
        o->open(path);
        return o;
    }
    ~Opened()
    {
        if (handle.isValid()) (void)backend.close(handle);
    }
};

} // namespace

// =============================================================================================
// A-89's engine half -- the generator's segments, read by the engine
// =============================================================================================

TEST_CASE("the engine reads measure-scale's paths as the generator drew them", "[measuregeometry][A-89][D6-14]")
{
    auto file = Opened::corpus("ordinary/measure-scale.pdf");
    Opened& opened = *file;
    PageGeometry g;
    REQUIRE(opened.backend.pageGeometry(opened.handle, 0, g));
    CHECK_FALSE(g.overLimit());
    const QJsonArray segments = label().value("geometry").toObject().value("segments").toArray();
    REQUIRE(segments.size() == 8);
    std::size_t straight = 0;
    for (const PathSegment& s : g.segments()) straight += s.curve ? 0 : 1;
    // EXACTLY the generator's eight: the heading and the labels are text, not paths, and are not read.
    CHECK(straight == 8);
    for (const QJsonValue value : segments) {
        const QJsonArray s = value.toArray();
        INFO(s[0].toDouble() << "," << s[1].toDouble() << " - " << s[2].toDouble() << "," << s[3].toDouble());
        CHECK(holds(g, PdfPoint{s[0].toDouble(), s[1].toDouble()}, PdfPoint{s[2].toDouble(), s[3].toDouble()}));
    }
    // And every one of the label's POINTS is a target there -- an endpoint, a midpoint or a crossing -- at a tolerance
    // of a hundredth of a point: what the snap resolver will be offered.
    for (const QJsonValue value : label().value("geometry").toObject().value("points").toArray()) {
        const QJsonObject p = value.toObject();
        const PdfPoint at{p.value("x").toDouble(), p.value("y").toDouble()};
        INFO(p.value("kind").toString().toStdString() << " " << at.x << "," << at.y);
        const std::vector<core::SnapPoint> found = core::snapPointsNear(g, at, 0.01, false);
        REQUIRE_FALSE(found.empty());
        CHECK(same(found.front().at, at));
    }
    // PAGE 2 has no path at all -- the control: SUCCESS, and nothing to snap to.
    PageGeometry none;
    REQUIRE(opened.backend.pageGeometry(opened.handle, 1, none));
    CHECK(none.segments().empty());
    CHECK_FALSE(none.overLimit());
    // A page that does not exist is refused, and leaves `into` untouched.
    PageGeometry untouched(7, {PathSegment{PdfPoint{1, 1}, PdfPoint{2, 2}, false}});
    CHECK_FALSE(opened.backend.pageGeometry(opened.handle, 5, untouched));
    CHECK(untouched.page() == 7);
}

TEST_CASE("a form's paths are read through its matrix and a curve's ends are its anchors", "[measuregeometry][D6-14]")
{
    // A form drawing a 100-point line from its origin, placed at (50, 50) scaled by 2: on the page (50, 50)-(250, 50).
    // And a Bezier from (300, 300) to (400, 400): eight pieces marked curve, its two ends anchors. A clip-only path --
    // `re W n` -- is neither stroked nor filled, and is not read.
    const std::string bytes = onePage("q 2 0 0 2 50 50 cm /Fm0 Do Q 0 G 1 w 300 300 m 300 350 350 400 400 400 c S "
                                      "10 10 500 500 re W n",
                                      [](tests::TinyPdf& pdf) {
                                          const int form = pdf.add(stream("/Type /XObject /Subtype /Form /BBox [ 0 0 200 200 ]",
                                                                          "0 G 1 w 0 0 m 100 0 l S"));
                                          return "/XObject << /Fm0 " + std::to_string(form) + " 0 R >>";
                                      });
    auto opened = Opened::built(bytes);
    PageGeometry g;
    REQUIRE(opened->backend.pageGeometry(opened->handle, 0, g));
    CHECK(holds(g, PdfPoint{50, 50}, PdfPoint{250, 50}));
    std::size_t curves = 0, straight = 0;
    for (const PathSegment& s : g.segments()) (s.curve ? curves : straight) += 1;
    CHECK(curves == 8);
    CHECK(straight == 1);   // the form's line, and no edge of the clip
    REQUIRE(g.anchors().size() == 2);
    CHECK(same(g.anchors()[0], PdfPoint{300, 300}));
    CHECK(same(g.anchors()[1], PdfPoint{400, 400}));
    // The curve at its middle -- B(0.5) = (300 + 3 x 300 + 3 x 350 + 400) / 8, (300 + 3 x 350 + 3 x 400 + 400) / 8
    // = (331.25, 368.75): no corner is offered there, but a point ON the curve is.
    const std::vector<core::SnapPoint> onCurve = core::snapPointsNear(g, PdfPoint{331.25, 368.75}, 2.0, true);
    REQUIRE_FALSE(onCurve.empty());
    CHECK(onCurve.front().kind == core::SnapPointKind::OnPath);
}

// =============================================================================================
// A-91 -- bounded, on the render thread, and kept
// =============================================================================================

TEST_CASE("a page of more segments than the bound is answered with no targets", "[measuregeometry][A-91][D6-14]")
{
    // 150,000 segments, each `x y m x y l`, in one stroked path.
    std::string content = "0 G 0.1 w ";
    content.reserve(150000 * 24);
    for (int i = 0; i < 150000; ++i) {
        const int y = i % 700;
        content += std::to_string(i % 500) + " " + std::to_string(y) + " m " + std::to_string(i % 500 + 3) + " "
                   + std::to_string(y) + " l ";
    }
    content += "S";
    auto opened = Opened::built(onePage(content));
    PageGeometry g;
    REQUIRE(opened->backend.pageGeometry(opened->handle, 0, g));
    CHECK(g.overLimit());
    // THE WALK STOPPED AT THE BOUND -- the 100,001st segment -- rather than reading all 150,000 and letting the model
    // refuse them: the bound is there for the time and the memory of the read, not only for what is kept.
    CHECK(g.counted() == PageGeometry::kMaxSegments + 1);
    CHECK(g.segments().empty());
    CHECK(core::snapPointsNear(g, PdfPoint{0, 0}, 8.0, true).empty());
}

TEST_CASE("pageGeometry runs on the render thread as an engine task and a page is read once",
          "[measuregeometry][A-91][D6-14]")
{
    pdf::DocumentBackendImpl backend;
    core::render::RenderScheduler scheduler{
        nullptr, [&backend] { return std::unique_ptr<core::ITextExtractor>(new app::BackendTextExtractor(backend)); },
        nullptr, core::render::RenderScheduler::Config{}};
    core::text::DocumentIndex index{scheduler};
    std::mutex mutex;
    std::set<std::thread::id> threads;
    int calls = 0;
    index.setGeometryReader([&](core::DocumentHandle handle, int page, PageGeometry& into) {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            threads.insert(std::this_thread::get_id());
            ++calls;
        }
        return backend.pageGeometry(handle, page, into);
    });
    auto opened = backend.open(corpusFile("ordinary/measure-scale.pdf"), nullptr);
    REQUIRE(opened);
    const core::DocumentHandle handle = opened.value();
    const core::text::DocumentIndex::EntryId entry =
        index.attach(backend.documentId(handle), handle, backend.pages(handle)->count());
    const core::text::DocumentIndex::Owner owner = index.newOwner();

    const auto pumpUntil = [&](const std::function<bool()>& done) {
        QElapsedTimer timer;
        timer.start();
        while (!done() && timer.elapsed() < 20000) {
            scheduler.waitForIdle(20);
            scheduler.deliverReady();
        }
    };
    CHECK_FALSE(index.requestGeometry(owner, entry, 0, core::render::RenderPriority::Visible));
    pumpUntil([&] { return index.geometry(entry, 0) != nullptr; });
    REQUIRE(index.geometry(entry, 0) != nullptr);
    CHECK(index.geometry(entry, 0)->segments().size() == 8);
    {
        const std::lock_guard<std::mutex> lock(mutex);
        REQUIRE(threads.size() == 1);
        CHECK(*threads.begin() != std::this_thread::get_id());   // never the asking (UI) thread
        CHECK(calls == 1);
    }
    CHECK(scheduler.stats().taskEnqueued == 1);
    // THE SECOND REQUEST asks the engine for nothing: no task, no call.
    CHECK(index.requestGeometry(owner, entry, 0, core::render::RenderPriority::Visible));
    scheduler.waitForIdle(2000);
    scheduler.deliverReady();
    CHECK(scheduler.stats().taskEnqueued == 1);
    {
        const std::lock_guard<std::mutex> lock(mutex);
        CHECK(calls == 1);
    }
    index.release(owner);
    index.detach(handle);
    scheduler.cancelDocument(handle);
    scheduler.waitForIdle(10000);
    (void)backend.close(handle);
}
