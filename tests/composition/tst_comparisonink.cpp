// tst_comparisonink -- Stage 5 T13, guide cases A-104 to A-107 over REAL pages (`Stage5-TextSearch-Testing-Guide.md`
// §5.15): the comparison through the product's own composition -- the engine extracting and rasterizing, the
// index, `ComparisonRun` on its own thread -- over `corpus/compare/`'s generated pairs, whose page counts and
// changes `corpus/make-compare.py` records (`*.expected.json`).
//
//   * **A-104.** Side by side scrolls by ALIGNED page: after an inserted page, the older document's page n is
//     shown beside the newer's n + 1, not beside n.
//   * **A-105.** The overlay composites both, judged on its RASTER: where the pair is identical every pixel is
//     grey -- one image -- and where it differs both tints are there.
//   * **A-106.** `Ctrl+]` and `Ctrl+[` as key events step both views and the pane, and wrap with a message.
//   * **A-107.** A comparison changes neither document: both tabs' journals empty, both files byte-identical
//     and unmoved in time after the comparison -- at *appearance*, its rasters asked for -- and its view closed.
//   * **A-127** (S5 guide I-24, 2026-10-04). The overlay's wheel, keys and zoom: a fitted pair turns a notch at a
//     time, a zoomed one scrolls and turns at its edge, only the region in view is rasterized, a stepped-to
//     difference is brought into view with its tints, and `Ctrl`+wheel zooms about the cursor. The wheel is SENT
//     to the canvas, below Windows' input queue -- a real mouse's notches are the user's to judge.
//
// WHY `tests/composition` (as `tst_selectionink`): real pages need the engine, which `rawpage_ui_tests` links
// none of. The widget halves -- the chord, the dialog, the claimed keys -- are `tests/ui/tst_comparison.cpp`.
//
// WHAT THIS DOES NOT COVER (convention 19): whether what the comparison reports reads as the change a person
// made -- manual case M-08; and every pair here is OURS, base-14 Helvetica, one kind of change each (D5-23).

#include <catch2/catch_test_macros.hpp>

#include "app/CompositionRoot.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Page.h"
#include "core/doc/PageTree.h"
#include "core/render/RenderScheduler.h"
#include "tests/render/TextOracle.h"
#include "ui/panes/DifferencesPane.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/ComparisonView.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ViewportController.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

using namespace rawpage;
using core::text::CompareSensitivity;
using core::text::CompareSide;
using core::text::Difference;

namespace {

struct Product {
    QTemporaryDir                         scratch;
    std::unique_ptr<app::CompositionRoot> root;
    std::unique_ptr<ui::MainWindow>       window;

    Product()
    {
        REQUIRE(scratch.isValid());
        app::CompositionRoot::Options options;
        options.settingsPath = QDir(scratch.path()).filePath(QStringLiteral("settings.ini"));
        options.sessionPath = QDir(scratch.path()).filePath(QStringLiteral("session.json"));
        options.recoveryRoot = QDir(scratch.path()).filePath(QStringLiteral("recovery"));
        root = std::make_unique<app::CompositionRoot>(std::move(options));
        window = std::make_unique<ui::MainWindow>(root->services());
        window->resize(1200, 900);
        window->show();
        QCoreApplication::processEvents();
    }

    ~Product()
    {
        if (ui::ComparisonView* comparison = window->comparison()) delete comparison;
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        root.reset();
    }

    // A COPY of a corpus file in the scratch directory: the corpus is generated and checked byte for byte,
    // and nothing here may be the reason it changed.
    QString copy(const char* relative) const
    {
        const QString source = test_oracle::corpusPath(relative);
        const QString target = QDir(scratch.path()).filePath(QFileInfo(source).fileName());
        QFile::remove(target);
        REQUIRE(QFile::copy(source, target));
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        return QFileInfo(target).absoluteFilePath();
    }

    ui::ComparisonView& compare(const QString& older, const QString& newer, CompareSensitivity sensitivity,
                                bool ignorePageNumbers = false)
    {
        ui::CompareDialog::Choice choice;
        choice.olderPath = older;
        choice.newerPath = newer;
        choice.options.sensitivity = sensitivity;
        choice.options.ignorePageNumbers = ignorePageNumbers;
        const core::Result<ui::ComparisonView*> opened = window->compareDocuments(choice);
        REQUIRE(opened);
        ui::ComparisonView& comparison = *opened.value();
        settle(comparison);
        return comparison;
    }

    void pump(int ms = 40) const
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, ms);
        root->services().renderScheduler.waitForIdle(ms);
        QCoreApplication::processEvents(QEventLoop::AllEvents, ms);
    }

    void settle(ui::ComparisonView& comparison) const
    {
        QElapsedTimer timer;
        timer.start();
        while (!comparison.isFinished() && timer.elapsed() < 30000) pump();
        REQUIRE(comparison.isFinished());
        for (QWidget* widget : comparison.findChildren<QWidget*>())
            if (QLayout* layout = widget->layout()) layout->activate();
        for (int round = 0; round < 6; ++round) pump();
    }

    static bool key(QWidget* target, Qt::Key key, Qt::KeyboardModifiers modifiers)
    {
        QKeyEvent press(QEvent::KeyPress, key, modifiers);
        press.ignore();
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers);
        QCoreApplication::sendEvent(target, &release);
        QCoreApplication::processEvents();
        return press.isAccepted();
    }
};

// The page at a view's top edge, and how far down it that edge is (0 at its top, 1 at its bottom).
struct Top {
    int    page = -1;
    double fraction = 0.0;
};

Top topOf(const ui::PageView& view)
{
    const core::DeviceRect visible = view.controller().visibleRect();
    for (const core::render::PageSlot& slot : view.layoutEngine().layout().pageSlots()) {
        if (slot.rect.bottom() > visible.top())
            return Top{slot.pageIndex, (visible.top() - slot.rect.top()) / std::max(1.0, slot.rect.size.height)};
    }
    return Top{};
}

// Puts `page` of `view` at the view's top edge, `fraction` of the way down it -- a person's scroll.
void scrollTo(ui::PageView& view, int page, double fraction)
{
    const core::render::PageSlot* slot = view.layoutEngine().layout().slotForPage(page);
    REQUIRE(slot != nullptr);
    view.controller().setScroll(core::DevicePoint{view.controller().scroll().x,
                                                  slot->rect.top() + fraction * slot->rect.size.height});
    QCoreApplication::processEvents();
}

QByteArray sha256Of(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}

// A pixel of the overlay's composite, judged: RED is (newer white, older ink), BLUE (newer ink, older white).
bool isRed(QRgb pixel) { return qRed(pixel) - qBlue(pixel) > 100; }
bool isBlue(QRgb pixel) { return qBlue(pixel) - qRed(pixel) > 100; }

// A page-space rectangle on the overlay's composite of an UPRIGHT page at `scale` device pixels per point.
QRect onComposite(const core::Page& page, const core::PdfRect& rect, double scale, int grow)
{
    const core::PdfRect crop = page.cropBox();
    return QRect(QPoint(static_cast<int>(std::floor((rect.left() - crop.left()) * scale)) - grow,
                        static_cast<int>(std::floor((crop.top() - rect.top()) * scale)) - grow),
                 QPoint(static_cast<int>(std::ceil((rect.right() - crop.left()) * scale)) + grow,
                        static_cast<int>(std::ceil((crop.top() - rect.bottom()) * scale)) + grow));
}

} // namespace

TEST_CASE("side by side scrolls by aligned page after an inserted page", "[comparisonink][A-104]")
{
    Product product;
    // base.pdf has three pages and page-inserted.pdf four, the second inserted (`page-inserted.expected.json`).
    // Page numbers ignored, so every later page is IDENTICAL to its counterpart (A-101's measurement).
    ui::ComparisonView& comparison = product.compare(product.copy("compare/base.pdf"),
                                                     product.copy("compare/page-inserted.pdf"),
                                                     CompareSensitivity::TextOnly, /*ignorePageNumbers=*/true);
    ui::PageView& older = *comparison.view(CompareSide::Base);
    ui::PageView& newer = *comparison.view(CompareSide::Revised);
    REQUIRE(older.pageCount() == 3);
    REQUIRE(newer.pageCount() == 4);
    CHECK(comparison.partnerOf(CompareSide::Base, 0) == 0);
    CHECK(comparison.partnerOf(CompareSide::Base, 1) == 2);
    CHECK(comparison.partnerOf(CompareSide::Base, 2) == 3);
    CHECK(comparison.partnerOf(CompareSide::Revised, 1) == -1);

    // THE OLDER'S SECOND PAGE, A THIRD OF THE WAY DOWN -> the newer's THIRD, a third of the way down. A pixel
    // mirror would show the newer's second page here: the control is that the partner is NOT the same index.
    scrollTo(older, 1, 1.0 / 3.0);
    Top beside = topOf(newer);
    CHECK(beside.page == 2);
    CHECK(beside.page != topOf(older).page);
    CHECK(std::abs(beside.fraction - 1.0 / 3.0) < 0.02);

    // THE LAST PAGES: a view cannot scroll its last page far up, so the older's own place is clamped -- and the
    // newer is put at THAT place, the fraction the older actually shows.
    scrollTo(older, 2, 0.5);
    beside = topOf(newer);
    CHECK(beside.page == 3);
    CHECK(std::abs(beside.fraction - topOf(older).fraction) < 0.02);

    // THE OTHER WAY: the newer leading, its third page -> the older's second.
    scrollTo(newer, 2, 0.25);
    CHECK(topOf(older).page == 1);
    CHECK(std::abs(topOf(older).fraction - 0.25) < 0.02);

    // THE INSERTED PAGE HAS NO PARTNER: the older holds at the top of the next page that has one.
    scrollTo(newer, 1, 0.5);
    const Top held = topOf(older);
    CHECK(held.page == 1);
    CHECK(held.fraction < 0.02);
}

TEST_CASE("side by side keeps the report pair on corresponding pages after its inserted page", "[comparisonink][A-104]")
{
    Product product;
    // report-v1.pdf has four pages and report-v2.pdf five, a page inserted at the third (`report.expected.json`).
    ui::ComparisonView& comparison = product.compare(product.copy("compare/report-v1.pdf"),
                                                     product.copy("compare/report-v2.pdf"), CompareSensitivity::TextOnly);
    ui::PageView& older = *comparison.view(CompareSide::Base);
    ui::PageView& newer = *comparison.view(CompareSide::Revised);
    REQUIRE(older.pageCount() == 4);
    REQUIRE(newer.pageCount() == 5);
    for (int page = 0; page < 4; ++page) {
        scrollTo(older, page, 0.1);
        const int expected = page < 2 ? page : page + 1;
        CHECK(topOf(newer).page == expected);
    }
}

TEST_CASE("the overlay is one grey image where the pair is identical and both tints where it differs", "[comparisonink][A-105]")
{
    Product product;
    // AT TEXT AND LAYOUT: a replaced word of another length moves the words after it along its line, and the
    // overlay shows that truthfully -- so "grey everywhere else" holds only at the sensitivity that REPORTS
    // those shifts. Every reported rectangle on the pair is the changed area; the rest must be grey.
    ui::ComparisonView& comparison = product.compare(product.copy("compare/base.pdf"),
                                                     product.copy("compare/word-replaced.pdf"),
                                                     CompareSensitivity::TextAndLayout);
    const std::vector<Difference>& all = comparison.differences();
    const auto replaced = std::find_if(all.begin(), all.end(),
                                       [](const Difference& d) { return d.kind == core::text::DifferenceKind::Replace; });
    REQUIRE(replaced != all.end());
    REQUIRE(std::count_if(all.begin(), all.end(),
                          [](const Difference& d) { return d.kind == core::text::DifferenceKind::Replace; }) == 1);
    const Difference difference = *replaced;
    const int replacedIndex = static_cast<int>(replaced - all.begin());
    REQUIRE_FALSE(difference.baseRects.empty());
    REQUIRE_FALSE(difference.revisedRects.empty());

    // A LARGER CANVAS, so a glyph is more than a few pixels and the tints have room to show.
    comparison.resize(1400, 2000);
    product.pump();
    comparison.setMode(ui::ComparisonView::Mode::Overlay);
    comparison.goToDifference(replacedIndex);
    QElapsedTimer timer;
    timer.start();
    while (comparison.overlayImage().isNull() && timer.elapsed() < 10000) product.pump();
    const QImage composite = comparison.overlayImage().convertToFormat(QImage::Format_RGB32);
    REQUIRE_FALSE(composite.isNull());
    const auto [olderPage, newerPage] = comparison.overlayPairs()[static_cast<std::size_t>(comparison.overlayPair())];
    REQUIRE(olderPage == difference.basePage);
    REQUIRE(newerPage == difference.revisedPage);

    const core::PageTree* olderPages = product.root->services().backend.pages(comparison.handle(CompareSide::Base));
    const core::PageTree* newerPages = product.root->services().backend.pages(comparison.handle(CompareSide::Revised));
    const double scale = comparison.overlayScale();
    // THE REPLACED WORD'S OWN AREA, and every rectangle reported on this pair.
    QRect word;
    for (const core::PdfRect& rect : difference.baseRects) word |= onComposite(*olderPages->at(olderPage), rect, scale, 3);
    for (const core::PdfRect& rect : difference.revisedRects) word |= onComposite(*newerPages->at(newerPage), rect, scale, 3);
    std::vector<QRect> reported;
    for (const Difference& d : comparison.differences()) {
        if (d.basePage == olderPage)
            for (const core::PdfRect& rect : d.baseRects) reported.push_back(onComposite(*olderPages->at(olderPage), rect, scale, 3));
        if (d.revisedPage == newerPage)
            for (const core::PdfRect& rect : d.revisedRects) reported.push_back(onComposite(*newerPages->at(newerPage), rect, scale, 3));
    }
    const auto inReported = [&reported](int x, int y) {
        return std::any_of(reported.begin(), reported.end(), [x, y](const QRect& r) { return r.contains(x, y); });
    };

    // WHERE IT DIFFERS, BOTH TINTS: the old word in red and the new one in blue.
    int red = 0, blue = 0, tintedOutside = 0;
    for (int y = 0; y < composite.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(composite.constScanLine(y));
        for (int x = 0; x < composite.width(); ++x) {
            const QRgb pixel = row[x];
            if (word.contains(x, y)) {
                red += isRed(pixel) ? 1 : 0;
                blue += isBlue(pixel) ? 1 : 0;
            }
            if (!inReported(x, y) && qRed(pixel) != qBlue(pixel)) ++tintedOutside;   // same ink in both: R equal to B
        }
    }
    INFO("red " << red << ", blue " << blue << ", tinted outside the reported areas " << tintedOutside
                << ", scale " << scale);
    CHECK(red > 40);
    CHECK(blue > 40);
    CHECK(tintedOutside == 0);
}

TEST_CASE("the overlay of an identical pair is grey on every page", "[comparisonink][A-105]")
{
    Product product;
    ui::ComparisonView& comparison = product.compare(product.copy("compare/base.pdf"),
                                                     product.copy("compare/identical.pdf"), CompareSensitivity::TextOnly);
    CHECK(comparison.differences().empty());
    comparison.setMode(ui::ComparisonView::Mode::Overlay);
    REQUIRE(comparison.overlayPairs().size() == 3);
    for (int pair = 0; pair < 3; ++pair) {
        comparison.setOverlayPair(pair);
        QElapsedTimer timer;
        timer.start();
        QImage composite;
        // The composite of THIS pair: rebuilt when both of its rasters have arrived.
        while (timer.elapsed() < 10000) {
            product.pump();
            composite = comparison.overlayImage();
            if (!composite.isNull() && comparison.overlayPair() == pair) break;
        }
        composite = composite.convertToFormat(QImage::Format_RGB32);
        REQUIRE_FALSE(composite.isNull());
        int ink = 0, tinted = 0;
        for (int y = 0; y < composite.height(); ++y) {
            const auto* row = reinterpret_cast<const QRgb*>(composite.constScanLine(y));
            for (int x = 0; x < composite.width(); ++x) {
                if (qRed(row[x]) != qBlue(row[x]) || qGreen(row[x]) != qBlue(row[x])) ++tinted;
                if (qGray(row[x]) < 128) ++ink;
            }
        }
        CHECK(ink > 1000);   // the control: there IS a page in it, and it is grey
        CHECK(tinted == 0);
    }
}

TEST_CASE("the overlay shows a page only one side has in that side's tint", "[comparisonink][A-105]")
{
    // THE DIRECTION, which "both tints are there" does not fix: a page only the NEWER has is all blue, and one
    // only the OLDER has all red -- the legend's words, and the side-by-side marks' colours.
    Product product;
    const auto inkOf = [&product](ui::ComparisonView& comparison, int olderPage, int newerPage) {
        comparison.setMode(ui::ComparisonView::Mode::Overlay);
        int pair = -1;
        for (std::size_t p = 0; p < comparison.overlayPairs().size(); ++p)
            if (comparison.overlayPairs()[p] == std::make_pair(olderPage, newerPage)) pair = static_cast<int>(p);
        REQUIRE(pair >= 0);
        comparison.setOverlayPair(pair);
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 2000) product.pump();
        const QImage composite = comparison.overlayImage().convertToFormat(QImage::Format_RGB32);
        REQUIRE_FALSE(composite.isNull());
        struct Counts { int red = 0, blue = 0, grey = 0; } counts;
        for (int y = 0; y < composite.height(); ++y) {
            const auto* row = reinterpret_cast<const QRgb*>(composite.constScanLine(y));
            for (int x = 0; x < composite.width(); ++x) {
                if (isRed(row[x])) ++counts.red;
                else if (isBlue(row[x])) ++counts.blue;
                else if (qGray(row[x]) < 128) ++counts.grey;
            }
        }
        return counts;
    };

    {
        ui::ComparisonView& inserted = product.compare(product.copy("compare/base.pdf"),
                                                       product.copy("compare/page-inserted.pdf"),
                                                       CompareSensitivity::TextOnly, true);
        const auto counts = inkOf(inserted, -1, 1);
        INFO("inserted page: red " << counts.red << ", blue " << counts.blue << ", grey ink " << counts.grey);
        CHECK(counts.blue > 1000);
        CHECK(counts.red == 0);
        CHECK(counts.grey == 0);
        inserted.close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    {
        ui::ComparisonView& deleted = product.compare(product.copy("compare/base.pdf"),
                                                      product.copy("compare/page-deleted.pdf"),
                                                      CompareSensitivity::TextOnly, true);
        int olderOnly = -1;
        for (const auto& [older, newer] : deleted.overlayPairs())
            if (newer < 0) olderOnly = older;
        REQUIRE(olderOnly >= 0);
        const auto counts = inkOf(deleted, olderOnly, -1);
        INFO("deleted page: red " << counts.red << ", blue " << counts.blue << ", grey ink " << counts.grey);
        CHECK(counts.red > 1000);
        CHECK(counts.blue == 0);
        CHECK(counts.grey == 0);
    }
}

TEST_CASE("Ctrl+] and Ctrl+[ step both views and the pane through the report pair and wrap", "[comparisonink][A-106]")
{
    Product product;
    ui::ComparisonView& comparison = product.compare(product.copy("compare/report-v1.pdf"),
                                                     product.copy("compare/report-v2.pdf"), CompareSensitivity::TextOnly);
    const std::vector<Difference> differences = comparison.differences();
    REQUIRE(differences.size() >= 5);
    REQUIRE(comparison.differencesPane()->rowCount() == static_cast<int>(differences.size()));

    // THE ORDER IS THE PAGES': each difference on an aligned pair no earlier than the one before it.
    const auto pairOf = [&](const Difference& d) {
        const auto& pairs = comparison.overlayPairs();
        for (std::size_t p = 0; p < pairs.size(); ++p)
            if ((d.revisedPage >= 0 && pairs[p].second == d.revisedPage) || (d.basePage >= 0 && pairs[p].first == d.basePage))
                return static_cast<int>(p);
        return -1;
    };
    for (std::size_t i = 1; i < differences.size(); ++i) CHECK(pairOf(differences[i - 1]) <= pairOf(differences[i]));

    comparison.activateWindow();
    ui::PageView& newer = *comparison.view(CompareSide::Revised);
    ui::PageView& older = *comparison.view(CompareSide::Base);
    newer.setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();

    // Whether a side SHOWS a difference: its rectangle on screen, or -- for the side the change is not on --
    // the page aligned with the other side's.
    const auto shows = [&](ui::PageView& view, const Difference& d, CompareSide side) {
        const int page = side == CompareSide::Base ? d.basePage : d.revisedPage;
        const std::vector<core::PdfRect>& rects = side == CompareSide::Base ? d.baseRects : d.revisedRects;
        const QRectF viewport(QPointF(0, 0), QSizeF(view.viewport()->size()));
        if (page >= 0 && !rects.empty()) {
            const std::optional<QRectF> at = view.widgetRectOf(page, rects.front());
            return at.has_value() && viewport.intersects(*at);
        }
        if (page >= 0) {   // a whole page: on screen
            const core::render::PageSlot* slot = view.layoutEngine().layout().slotForPage(page);
            const core::DeviceRect visible = view.controller().visibleRect();
            return slot != nullptr && slot->rect.bottom() > visible.top() && slot->rect.top() < visible.bottom();
        }
        const int otherPage = side == CompareSide::Base ? d.revisedPage : d.basePage;
        const int partner = comparison.partnerOf(side == CompareSide::Base ? CompareSide::Revised : CompareSide::Base, otherPage);
        const core::DeviceRect visible = view.controller().visibleRect();
        // No partner: the other side holds at the next aligned page -- anything on screen.
        if (partner < 0) return true;
        const core::render::PageSlot* slot = view.layoutEngine().layout().slotForPage(partner);
        return slot != nullptr && slot->rect.bottom() > visible.top() && slot->rect.top() < visible.bottom();
    };

    for (std::size_t i = 0; i < differences.size(); ++i) {
        CHECK(Product::key(&newer, Qt::Key_BracketRight, Qt::ControlModifier));
        product.pump();
        INFO("difference " << i << ": " << ui::DifferencesPane::kindText(differences[i]).toStdString());
        CHECK(comparison.currentDifference() == static_cast<int>(i));
        CHECK(comparison.differencesPane()->current() == static_cast<int>(i));
        CHECK(shows(older, differences[i], CompareSide::Base));
        CHECK(shows(newer, differences[i], CompareSide::Revised));
    }

    // THE ENDS WRAP, WITH A MESSAGE.
    CHECK(Product::key(&newer, Qt::Key_BracketRight, Qt::ControlModifier));
    CHECK(comparison.currentDifference() == 0);
    CHECK(comparison.statusText() == ui::catalog::translate(ui::catalog::key::CompareStatusWrappedToFirst));
    CHECK(Product::key(&newer, Qt::Key_BracketLeft, Qt::ControlModifier));
    CHECK(comparison.currentDifference() == static_cast<int>(differences.size()) - 1);
    CHECK(comparison.statusText() == ui::catalog::translate(ui::catalog::key::CompareStatusWrappedToLast));
    CHECK(Product::key(&newer, Qt::Key_BracketLeft, Qt::ControlModifier));
    CHECK(comparison.currentDifference() == static_cast<int>(differences.size()) - 2);

    // A ROW CHOSEN IN THE PANE IS THE SAME STEP.
    comparison.differencesPane()->list()->setCurrentItem(comparison.differencesPane()->list()->topLevelItem(2));
    CHECK(comparison.currentDifference() == 2);
}

TEST_CASE("a comparison changes neither document", "[comparisonink][A-107]")
{
    Product product;
    const QString older = product.copy("compare/report-v1.pdf");
    const QString newer = product.copy("compare/report-v2.pdf");
    const QByteArray olderHash = sha256Of(older);
    const QByteArray newerHash = sha256Of(newer);
    const QDateTime olderTime = QFileInfo(older).lastModified();
    const QDateTime newerTime = QFileInfo(newer).lastModified();

    // BOTH OPEN IN TABS FIRST, so there are two journals a comparison could reach if anything did.
    product.window->openFiles({older, newer});
    REQUIRE(product.window->documentCount() == 2);
    for (int round = 0; round < 6; ++round) product.pump();

    {
        // AT APPEARANCE: every aligned pair's 72 dpi rasters asked for through the scheduler and diffed -- and
        // asked for BYPASSING the tile cache (D5-4): consumed once, they must not evict the page being read.
        const long long bypassedBefore = product.root->services().renderScheduler.stats().enqueuedBypassingCache;
        ui::ComparisonView& comparison = product.compare(older, newer, CompareSensitivity::Appearance);
        REQUIRE(comparison.result() != nullptr);
        CHECK(comparison.result()->rastersMissing == 0);
        const long long bypassed = product.root->services().renderScheduler.stats().enqueuedBypassingCache - bypassedBefore;
        int bothSides = 0;
        for (const core::text::PageMatch& match : comparison.result()->pages)
            if (match.base >= 0 && match.revised >= 0) ++bothSides;
        INFO("rasters bypassing the cache: " << bypassed << " for " << bothSides << " aligned pairs");
        CHECK(bypassed >= 2);
        CHECK(bypassed % 2 == 0);
        CHECK(bypassed <= 2 * bothSides);
        CHECK(comparison.result()->count(core::text::DifferenceKind::Appearance) >= 1);   // the moved image
        CHECK(comparison.handle(CompareSide::Base) != product.window->documentAt(0)->handle());

        // EVERYTHING IT OFFERS: every step, the overlay, the other sensitivities.
        for (std::size_t i = 0; i < comparison.differences().size(); ++i) comparison.nextDifference();
        comparison.setMode(ui::ComparisonView::Mode::Overlay);
        for (int pair = 0; pair < static_cast<int>(comparison.overlayPairs().size()); ++pair) {
            comparison.setOverlayPair(pair);
            product.pump();
        }
        comparison.setMode(ui::ComparisonView::Mode::SideBySide);
        comparison.close();
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    for (int round = 0; round < 4; ++round) product.pump();
    CHECK(product.window->comparison() == nullptr);

    // BOTH JOURNALS EMPTY, NO SAVE OFFERED: each tab closes clean, without a disposition.
    for (int index = 0; index < 2; ++index) {
        ui::DocumentWindow* document = product.window->documentAt(index);
        REQUIRE(document != nullptr);
        REQUIRE(document->journal() != nullptr);
        CHECK_FALSE(document->journal()->canUndo());
        CHECK_FALSE(document->isDirty());
    }
    CHECK(product.window->closeAllDocuments());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(product.window->documentCount() == 0);

    // BOTH FILES BYTE-IDENTICAL, AND UNMOVED IN TIME.
    CHECK(sha256Of(older) == olderHash);
    CHECK(sha256Of(newer) == newerHash);
    CHECK(QFileInfo(older).lastModified() == olderTime);
    CHECK(QFileInfo(newer).lastModified() == newerTime);
}

// ---- S5 guide I-24: the overlay zooms, scrolls, and turns at the edge (A-127) ------------------------------------

namespace {

// A wheel event as a mouse or touchpad sends it -- `angle` in `angleDelta` units, 120 a notch, negative DOWN --
// at the middle of what the overlay's viewport shows, or at `at` on the canvas.
void wheel(ui::ComparisonView& comparison, int angle, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
           std::optional<QPointF> at = std::nullopt)
{
    QWidget* canvas = comparison.findChild<QWidget*>(QStringLiteral("comparisonOverlay"));
    REQUIRE(canvas != nullptr);
    QWidget* viewport = comparison.overlayScrollArea()->viewport();
    const QPointF where = at.value_or(QPointF(canvas->mapFrom(viewport, viewport->rect().center())));
    QWheelEvent event(where, canvas->mapToGlobal(where), QPoint(), QPoint(0, angle), Qt::NoButton, modifiers,
                      Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(canvas, &event);
    QCoreApplication::processEvents();
}

QWidget* overlayCanvas(ui::ComparisonView& comparison)
{
    QWidget* canvas = comparison.findChild<QWidget*>(QStringLiteral("comparisonOverlay"));
    REQUIRE(canvas != nullptr);
    return canvas;
}

// The pair's page at the overlay's scale, in device pixels -- the whole raster the region is a part of.
QSize pageOnOverlay(Product& product, ui::ComparisonView& comparison)
{
    const auto [olderPage, newerPage] = comparison.overlayPairs()[static_cast<std::size_t>(comparison.overlayPair())];
    const core::PageTree* olderPages = product.root->services().backend.pages(comparison.handle(CompareSide::Base));
    const core::PageTree* newerPages = product.root->services().backend.pages(comparison.handle(CompareSide::Revised));
    double width = 0.0;
    double height = 0.0;
    for (const core::Page* page : {olderPage >= 0 ? olderPages->at(olderPage) : nullptr,
                                   newerPage >= 0 ? newerPages->at(newerPage) : nullptr}) {
        if (page == nullptr) continue;
        width = std::max(width, page->displaySize().width);
        height = std::max(height, page->displaySize().height);
    }
    return QSize(static_cast<int>(std::ceil(width * comparison.overlayScale())),
                 static_cast<int>(std::ceil(height * comparison.overlayScale())));
}

} // namespace

TEST_CASE("the overlay wheel turns a fitted pair and scrolls a zoomed one before turning at its edge", "[comparisonink][A-127][I-24]")
{
    Product product;
    ui::ComparisonView& comparison = product.compare(product.copy("compare/base.pdf"),
                                                     product.copy("compare/word-replaced.pdf"),
                                                     CompareSensitivity::TextAndLayout);
    comparison.setMode(ui::ComparisonView::Mode::Overlay);
    for (int round = 0; round < 6; ++round) product.pump();
    QWidget* canvas = overlayCanvas(comparison);
    QScrollBar* down = comparison.overlayScrollArea()->verticalScrollBar();
    REQUIRE(comparison.overlayPairs().size() >= 3);
    REQUIRE(comparison.overlayPair() == 0);
    CHECK(comparison.overlayFit() == core::render::FitMode::Page);

    // FITTED, NOTHING TO SCROLL: each notch turns -- the defect was that it did nothing at all.
    CHECK(down->maximum() == down->minimum());
    wheel(comparison, -120);
    CHECK(comparison.overlayPair() == 1);
    wheel(comparison, +120);
    CHECK(comparison.overlayPair() == 0);
    // A TOUCHPAD'S HALF NOTCHES add up to one turn, not one each.
    wheel(comparison, -60);
    CHECK(comparison.overlayPair() == 0);
    wheel(comparison, -60);
    CHECK(comparison.overlayPair() == 1);
    wheel(comparison, +120);
    REQUIRE(comparison.overlayPair() == 0);

    // ZOOMED, by the side views' chords on the overlay: Actual Size, then three rungs up the ladder to 200 %.
    CHECK(Product::key(canvas, Qt::Key_1, Qt::ControlModifier));
    CHECK(comparison.overlayFit() == core::render::FitMode::None);
    CHECK(comparison.overlayZoom().factor == 1.0);
    for (int step = 0; step < 3; ++step) CHECK(Product::key(canvas, Qt::Key_Equal, Qt::ControlModifier));
    CHECK(comparison.overlayZoom().factor == 2.0);
    down->setValue(down->minimum());
    for (int round = 0; round < 6; ++round) product.pump();
    REQUIRE(down->maximum() > down->minimum());

    // ONLY THE REGION IN VIEW IS RASTERIZED, from the page's top here.
    const QSize page = pageOnOverlay(product, comparison);
    const double dpr = canvas->devicePixelRatioF();
    const QSize viewport = comparison.overlayScrollArea()->viewport()->size();
    QImage composite = comparison.overlayImage();
    INFO("page " << page.width() << "x" << page.height() << ", composite " << composite.width() << "x"
                 << composite.height() << ", viewport " << viewport.width() << "x" << viewport.height());
    REQUIRE_FALSE(composite.isNull());
    CHECK(composite.height() < page.height());
    CHECK(composite.height() <= static_cast<int>(std::ceil(2.0 * viewport.height() * dpr)) + 2);
    CHECK(comparison.overlayOrigin().y() == 0);

    // A NOTCH INSIDE THE PAIR SCROLLS, and does not turn.
    const int top = down->value();
    wheel(comparison, -120);
    CHECK(comparison.overlayPair() == 0);
    CHECK(down->value() > top);

    // TO ITS BOTTOM, notch by notch -- the notch that arrives there does not turn.
    for (int notch = 0; notch < 400 && down->value() < down->maximum(); ++notch) wheel(comparison, -120);
    REQUIRE(down->value() == down->maximum());
    CHECK(comparison.overlayPair() == 0);
    for (int round = 0; round < 6; ++round) product.pump();
    composite = comparison.overlayImage();
    CHECK(comparison.overlayOrigin().y() > 0);
    CHECK(std::abs(comparison.overlayOrigin().y() + composite.height() - page.height()) <= 1);

    // ONE NOTCH AT THE EDGE TURNS: the next pair, from its top, at the same zoom -- and up at that top turns
    // back to the previous pair's bottom.
    wheel(comparison, -120);
    CHECK(comparison.overlayPair() == 1);
    CHECK(down->value() == down->minimum());
    CHECK(comparison.overlayZoom().factor == 2.0);
    wheel(comparison, +120);
    CHECK(comparison.overlayPair() == 0);
    CHECK(down->value() == down->maximum());

    // THE KEYS DO THE SAME: Page Down at the bottom turns, and inside the pair scrolls; Ctrl+Page Down turns.
    CHECK(Product::key(canvas, Qt::Key_PageDown, Qt::NoModifier));
    CHECK(comparison.overlayPair() == 1);
    CHECK(down->value() == down->minimum());
    CHECK(Product::key(canvas, Qt::Key_PageDown, Qt::NoModifier));
    CHECK(comparison.overlayPair() == 1);
    CHECK(down->value() > down->minimum());
    CHECK(Product::key(canvas, Qt::Key_PageDown, Qt::ControlModifier));
    CHECK(comparison.overlayPair() == 2);

    // AND Ctrl+0 FITS THE PAGE AGAIN: nothing to scroll, the whole page one composite at its origin.
    CHECK(Product::key(canvas, Qt::Key_0, Qt::ControlModifier));
    for (int round = 0; round < 6; ++round) product.pump();
    CHECK(comparison.overlayFit() == core::render::FitMode::Page);
    CHECK(down->maximum() == down->minimum());
    CHECK(comparison.overlayOrigin() == QPoint(0, 0));
    CHECK(comparison.overlayImage().size() == pageOnOverlay(product, comparison));
}

TEST_CASE("a zoomed overlay brings the difference it steps to into view with both tints", "[comparisonink][A-127][I-24]")
{
    Product product;
    ui::ComparisonView& comparison = product.compare(product.copy("compare/base.pdf"),
                                                     product.copy("compare/word-replaced.pdf"),
                                                     CompareSensitivity::TextAndLayout);
    const std::vector<Difference>& all = comparison.differences();
    const auto replaced = std::find_if(all.begin(), all.end(),
                                       [](const Difference& d) { return d.kind == core::text::DifferenceKind::Replace; });
    REQUIRE(replaced != all.end());
    const Difference difference = *replaced;

    // AT 400 %, SCROLLED TO THE PAIR'S FAR RIGHT: the change is off screen until it is stepped to. (From the top
    // left it is in view without being brought there -- the planted control "no reveal" stayed green so.)
    comparison.setMode(ui::ComparisonView::Mode::Overlay);
    comparison.setOverlayZoom(core::render::Zoom::percent(400));
    QScrollBar* across = comparison.overlayScrollArea()->horizontalScrollBar();
    REQUIRE(across->maximum() > across->minimum());
    across->setValue(across->maximum());
    for (int round = 0; round < 6; ++round) product.pump();
    comparison.goToDifference(static_cast<int>(replaced - all.begin()));
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2000) product.pump();

    const auto [olderPage, newerPage] = comparison.overlayPairs()[static_cast<std::size_t>(comparison.overlayPair())];
    REQUIRE(olderPage == difference.basePage);
    REQUIRE(newerPage == difference.revisedPage);
    const QImage composite = comparison.overlayImage().convertToFormat(QImage::Format_RGB32);
    REQUIRE_FALSE(composite.isNull());
    CHECK(composite.height() < pageOnOverlay(product, comparison).height());

    // THE WORD, ON THE COMPOSITE -- which starts at overlayOrigin() on the page -- and in the viewport.
    const core::PageTree* olderPages = product.root->services().backend.pages(comparison.handle(CompareSide::Base));
    const core::PageTree* newerPages = product.root->services().backend.pages(comparison.handle(CompareSide::Revised));
    const double scale = comparison.overlayScale();
    QRect word;
    for (const core::PdfRect& rect : difference.baseRects) word |= onComposite(*olderPages->at(olderPage), rect, scale, 3);
    for (const core::PdfRect& rect : difference.revisedRects) word |= onComposite(*newerPages->at(newerPage), rect, scale, 3);
    word.translate(-comparison.overlayOrigin());
    INFO("word " << word.x() << "," << word.y() << " " << word.width() << "x" << word.height() << " on a composite "
                 << composite.width() << "x" << composite.height());
    REQUIRE(composite.rect().contains(word));

    QWidget* canvas = overlayCanvas(comparison);
    const double dpr = canvas->devicePixelRatioF();
    const QRectF wordOnCanvas =
        QRectF(QPointF(word.topLeft() + comparison.overlayOrigin()) / dpr, QSizeF(word.size()) / dpr)
            .translated(comparison.overlayPageRect().topLeft());
    QWidget* viewport = comparison.overlayScrollArea()->viewport();
    const QRectF inView(QPointF(canvas->mapFrom(viewport, QPoint(0, 0))), QSizeF(viewport->size()));
    CHECK(inView.contains(wordOnCanvas));

    int red = 0, blue = 0;
    for (int y = word.top(); y <= word.bottom(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(composite.constScanLine(y));
        for (int x = word.left(); x <= word.right(); ++x) {
            red += isRed(row[x]) ? 1 : 0;
            blue += isBlue(row[x]) ? 1 : 0;
        }
    }
    INFO("red " << red << ", blue " << blue);
    CHECK(red > 40);
    CHECK(blue > 40);
}

TEST_CASE("Ctrl and the wheel zoom the overlay about the cursor", "[comparisonink][A-127][I-24]")
{
    Product product;
    ui::ComparisonView& comparison = product.compare(product.copy("compare/base.pdf"),
                                                     product.copy("compare/word-replaced.pdf"),
                                                     CompareSensitivity::TextOnly);
    comparison.setMode(ui::ComparisonView::Mode::Overlay);
    for (int round = 0; round < 6; ++round) product.pump();
    QWidget* canvas = overlayCanvas(comparison);
    QScrollArea* area = comparison.overlayScrollArea();

    // A POINT A THIRD OF THE WAY INTO THE PAGE, and where it is on the page in points.
    const QRectF page = comparison.overlayPageRect();
    const QPointF cursor = page.topLeft() + QPointF(page.width() / 3.0, page.height() / 3.0);
    const QPointF inViewport = cursor - QPointF(area->horizontalScrollBar()->value(), area->verticalScrollBar()->value());
    const double before = comparison.overlayScale() / canvas->devicePixelRatioF();
    const QPointF onPage = (cursor - page.topLeft()) / before;
    const double fitted = comparison.overlayZoom().factor;

    wheel(comparison, +120, Qt::ControlModifier, cursor);
    CHECK(comparison.overlayFit() == core::render::FitMode::None);
    CHECK(comparison.overlayZoom().factor == ui::ViewportController::nextZoomStep(core::render::Zoom{fitted}, +1).factor);

    // THE SAME PAGE POINT UNDER THE SAME PLACE IN THE VIEWPORT.
    const double after = comparison.overlayScale() / canvas->devicePixelRatioF();
    const QPointF under = inViewport + QPointF(area->horizontalScrollBar()->value(), area->verticalScrollBar()->value());
    const QPointF nowOnPage = (under - comparison.overlayPageRect().topLeft()) / after;
    INFO("before " << onPage.x() << "," << onPage.y() << " pt; after " << nowOnPage.x() << "," << nowOnPage.y() << " pt");
    CHECK(std::abs(nowOnPage.x() - onPage.x()) * after <= 1.5);
    CHECK(std::abs(nowOnPage.y() - onPage.y()) * after <= 1.5);

    // AND DOWN AGAIN, one rung.
    const double zoomed = comparison.overlayZoom().factor;
    wheel(comparison, -120, Qt::ControlModifier, under);
    CHECK(comparison.overlayZoom().factor == ui::ViewportController::nextZoomStep(core::render::Zoom{zoomed}, -1).factor);
}
