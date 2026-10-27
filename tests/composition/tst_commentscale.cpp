// tst_commentscale -- Stage 6 T9 through the product's composition and the shipped engine
// (`Stage6-Annotations-Testing-Guide.md` §3.6 and §5.12; A-73, A-74, A-72's tile half, A-36's search clause;
// decisions D6-11, D6-12).
//
//   * A-73: `annots-900.pdf`'s 9,000 comments -- the store hydrated from the file inside 500 ms, its live heap
//     inside 64 MB (LIVE heap, never private bytes: Stage 2's I-35), the pane populated inside 500 ms after it
//     is shown, and twenty group, sort and filter changes each ON SCREEN -- rebuilt AND painted -- inside 100 ms
//     at p95. Three runs, the figures printed.
//   * A-74 and A-36's search clause: a note and a highlight with a comment, made and NOT saved, found by the
//     find bar with `search/includeComments` on and not with it off -- comments of an open document are the
//     store's (D6-11) -- and the hit SELECTS the annotation it is in.
//   * A-72's tile half: numbering on paints numbers over the page and changes no pixel of its raster.
//
// WHAT IT DOES NOT COVER (convention 19): the folder search's half of A-74 is `tests/render/tst_foldersearch`'s;
// the attachment hit selecting in the Attachments pane is T11's (the pane does not exist yet). The timings are
// this machine's, offscreen; a real monitor's paint is M-05's to feel.

#include <catch2/catch_test_macros.hpp>

#include "tests/bench/HeapCensus.h"
#include "tests/composition/AnnotProduct.h"

#include "core/settings/Settings.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/PageOps.h"
#include "ui/panes/CommentsPane.h"
#include "ui/shell/SidebarDock.h"
#include "ui/write/FindReplaceBar.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTreeView>

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdio>
#include <vector>

using namespace rawpage;
using annot_product::Product;
using core::Annotation;
using core::AnnotationId;

namespace {

void sendKey(Product& product, Qt::Key key, Qt::KeyboardModifiers modifiers, const QString& text)
{
    QWidget* focus = QApplication::focusWidget();
    if (focus == nullptr) focus = product.view;
    QKeyEvent override(QEvent::ShortcutOverride, key, modifiers, text);
    override.ignore();
    QCoreApplication::sendEvent(focus, &override);
    QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
    QCoreApplication::sendEvent(focus, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
    QCoreApplication::sendEvent(focus, &release);
    QCoreApplication::processEvents();
}

// The query typed into the find bar from the page, and the search waited for -- the product's path.
std::vector<core::text::SearchHit> find(Product& product, const QString& query)
{
    ui::FindReplaceBar& bar = *product.document()->findBar();
    if (!bar.isOpen()) {
        product.view->setFocus(Qt::OtherFocusReason);
        sendKey(product, Qt::Key_F, Qt::ControlModifier, QString());
        REQUIRE(bar.isOpen());
    }
    bar.field()->setFocus(Qt::OtherFocusReason);
    bar.field()->selectAll();
    const int before = bar.searchesFinished();
    for (const QChar ch : query) sendKey(product, Qt::Key_unknown, Qt::NoModifier, QString(ch));
    QElapsedTimer timer;
    timer.start();
    while ((bar.isSearchPending() || bar.isSearching() || bar.searchesFinished() == before) && timer.elapsed() < 30000)
        product.settle();
    REQUIRE(bar.searchesFinished() > before);
    product.settle();
    return bar.hits();
}

double percentile95(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(values.size()))) - 1];
}

} // namespace

// ==========================================================================================
// A-73 -- the pane at 9,000 comments (guide §3.6)
// ==========================================================================================

TEST_CASE("the Comments pane over 9000 comments is inside every bound of the guide", "[commentscale][A-73][run-serial]")
{
    // THE STORE HYDRATED FROM THE FILE as the backend hydrates it at open -- the cross-reference, the page walk,
    // the first-party reader -- and its live heap, three times.
    QFile source(QStringLiteral(RAWPAGE_CORPUS_DIR "/ordinary/annots-900.pdf"));
    REQUIRE(source.open(QIODevice::ReadOnly));
    const QByteArray bytes = source.readAll();
    std::vector<double> hydrations;
    std::size_t heapBytes = 0;
    std::size_t annotations = 0;
    for (int run = 0; run < 3; ++run) {
        const std::size_t before = bench::heapCensus().busyBytes;
        QElapsedTimer timer;
        timer.start();
        // The cross-reference and the walk live in this block; the STORE outlives it and is what is measured.
        core::AnnotationStore store = [&] {
            auto opened = pdf::cos::openWithRepair(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
            REQUIRE(opened);
            auto walked = pdf::ops::PageOps::walk(opened.value().table);
            REQUIRE(walked);
            return core::AnnotationStore::fromReading(pdf::ops::AnnotationReader::read(opened.value().table, walked.value()));
        }();
        hydrations.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
        annotations = store.size();
        const std::size_t after = bench::heapCensus().busyBytes;
        heapBytes = std::max(heapBytes, after > before ? after - before : 0);
    }
    REQUIRE(annotations == 9000);

    // THE PANE, through the product: the file opened, the pane opened with its chord, populated; then twenty
    // changes of group, sort and filter, each timed from the change to the list PAINTED.
    Product product;
    product.open("ordinary/annots-900.pdf");
    ui::CommentsPane* pane = product.window->commentsPane();
    REQUIRE(pane != nullptr);
    std::vector<double> populations;
    std::vector<double> changes;
    using G = core::AnnotationStore::GroupKey;
    using S = core::AnnotationStore::SortKey;
    for (int run = 0; run < 3; ++run) {
        // Hidden behind another pane and told the document changed: the list is STALE, and showing the pane
        // builds it -- the population a person waits for.
        product.window->sidebar()->showPane(core::ActionId::PaneThumbnails);
        QCoreApplication::processEvents();
        REQUIRE_FALSE(pane->isVisible());
        pane->setGroupKey(G::None);
        pane->setSortKey(S::Page);
        pane->refresh();
        QElapsedTimer timer;
        timer.start();
        REQUIRE(product.root->services().actions.dispatch(core::ActionId::PaneComments, core::KeyContext::Global));
        pane->view()->viewport()->repaint();
        populations.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
        REQUIRE(pane->isVisible());
        REQUIRE(pane->listedCount() == 9000);
        REQUIRE(pane->visibleRowCount() > 0);

        const std::vector<std::function<void()>> steps{
            [&] { pane->setGroupKey(G::Page); },        [&] { pane->setSortKey(S::Date); },
            [&] { pane->setGroupKey(G::Author); },      [&] { pane->setSortKey(S::Author); },
            [&] { pane->setGroupKey(G::Date); },        [&] { pane->setSearchText(QStringLiteral("page 4")); },
            [&] { pane->setSearchText(QString()); },    [&] { pane->setGroupKey(G::Type); },
            [&] { pane->setTypeFilter({core::AnnotationSubtype::Highlight}); },
            [&] { pane->setTypeFilter({}); },           [&] { pane->setGroupKey(G::ReviewStatus); },
            [&] { pane->setStatusFilter({QStringLiteral("None")}); },
            [&] { pane->setStatusFilter({}); },         [&] { pane->setGroupKey(G::Checkmark); },
            [&] { pane->setCheckmarkFilter(false); },   [&] { pane->setCheckmarkFilter(std::nullopt); },
            [&] { pane->setSortKey(S::Subtype); },      [&] { pane->setGroupKey(G::None); },
            [&] { pane->setDateRange(0, 1); },          [&] { pane->setDateRange(std::nullopt, std::nullopt); },
        };
        REQUIRE(steps.size() == 20);
        for (const auto& step : steps) {
            QElapsedTimer one;
            one.start();
            step();
            pane->view()->viewport()->repaint();
            changes.push_back(static_cast<double>(one.nsecsElapsed()) / 1.0e6);
        }
        CHECK(pane->listedCount() == 9000);   // every filter taken off again
    }

    const double hydrated = *std::max_element(hydrations.begin(), hydrations.end());
    const double populated = *std::max_element(populations.begin(), populations.end());
    const double p95 = percentile95(changes);
    std::printf("A-73 annots-900: store hydrated %.1f / %.1f / %.1f ms, live heap %.1f MB; pane populated "
                "%.1f / %.1f / %.1f ms; %zu changes p95 %.1f ms, max %.1f ms\n",
                hydrations[0], hydrations[1], hydrations[2], heapBytes / 1048576.0, populations[0], populations[1],
                populations[2], changes.size(), p95, *std::max_element(changes.begin(), changes.end()));
    // THE GUIDE'S BOUND IS THE SHIPPED BUILD'S (A-22's precedent): an unoptimized `x64-debug` reader took 730-820 ms
    // over the same file at T9, and is graded on a ceiling of its own rather than on a number it was never meant to meet.
#ifdef NDEBUG
    constexpr double kHydrationBoundMs = 500.0;
#else
    constexpr double kHydrationBoundMs = 2000.0;
#endif
    CHECK(hydrated <= kHydrationBoundMs);
    CHECK(heapBytes <= 64u * 1024u * 1024u);
    CHECK(populated <= 500.0);
    CHECK(p95 <= 100.0);
}

// ==========================================================================================
// A-74 and A-36's search clause -- comments of an open document are searched from its store
// ==========================================================================================

TEST_CASE("an unsaved comment is found by the find bar and the hit selects it", "[commentscale][A-74][A-36][D6-11]")
{
    Product product;
    core::Settings& settings = product.root->services().settings;
    settings.set(core::keys::search::includeComments, true);
    product.open("ordinary/text-a4.pdf");
    ui::AnnotationOverlay* overlay = product.overlay();

    // A NOTE, made as the tool makes one, and a HIGHLIGHT carrying a comment -- neither saved.
    REQUIRE(overlay->addNote(0, core::PdfPoint{520, 760}, QStringLiteral("A marmoset was here"), {}));
    Annotation highlight = core::newAnnotation(core::AnnotationSubtype::Highlight, 0, QStringLiteral("Ana"), 1791590400);
    highlight.quadPoints = {72, 700, 200, 700, 72, 688, 200, 688};
    highlight.rect = *core::markupRect(highlight.quadPoints);
    highlight.color = {1, 1, 0};
    highlight.contents = QStringLiteral("Quokkas, not marmosets");
    REQUIRE(product.document()->journal()->run(
        std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{highlight})));
    product.settle();
    AnnotationId note;
    for (const Annotation& a : product.store().all())
        if (a.contents == QStringLiteral("A marmoset was here")) note = a.id;
    REQUIRE(note.isValid());
    REQUIRE(overlay->selection().empty());

    // FOUND, from the store: the note's hit is the current one, revealed and SELECTED.
    const std::vector<core::text::SearchHit> hits = find(product, QStringLiteral("marmoset"));
    REQUIRE(hits.size() == 2);   // the note's "marmoset", the highlight's "marmosets"
    for (const core::text::SearchHit& hit : hits) CHECK(hit.contentClass() == core::text::ContentClass::Comments);
    CHECK(overlay->selection().size() == 1);
    const std::optional<AnnotationId> selected = overlay->selection().empty() ? std::nullopt
                                                                              : std::optional(overlay->selection().front());
    REQUIRE(selected);
    CHECK((*selected == note || *selected == highlight.id));
    // The highlight's comment text (A-36's clause): a search for it finds it.
    const std::vector<core::text::SearchHit> quokkas = find(product, QStringLiteral("Quokkas"));
    REQUIRE(quokkas.size() == 1);
    CHECK(quokkas.front().unit->locator == highlight.id.toString());
    CHECK(overlay->selection() == std::vector<AnnotationId>{highlight.id});

    // THE CONTROL: comments switched off, nothing -- the file's text has no marmoset either.
    settings.set(core::keys::search::includeComments, false);
    CHECK(find(product, QStringLiteral("marmoset")).empty());
}

// ==========================================================================================
// A-72's tile half -- numbers are painted over the page and never into it
// ==========================================================================================

TEST_CASE("numbering paints numbers over the page and changes no pixel of its raster", "[commentscale][A-72]")
{
    Product product;
    product.open("ordinary/annots-threads.pdf");
    const QImage before = product.page(0, true);
    const QImage screenBefore = product.view->viewport()->grab().toImage();
    const std::size_t steps = product.document()->journal()->historySize();

    REQUIRE(product.root->services().actions.dispatch(core::ActionId::CommentNumbering, core::KeyContext::Global));
    product.settle();
    REQUIRE(product.overlay()->numbers().size() == 8);   // the label's eight top-level comments

    // ON SCREEN: the badges are there -- the view's grab differs from before.
    const QImage screenAfter = product.view->viewport()->grab().toImage();
    CHECK(annot_product::differingPixels(screenBefore, screenAfter) > 20);
    // IN THE PAGE: not one pixel, and no command.
    const QImage after = product.page(0, true);
    CHECK(annot_product::differingPixels(before, after) == 0);
    CHECK(product.document()->journal()->historySize() == steps);
}
