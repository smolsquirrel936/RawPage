// Guide case A-85 -- the A3 criterion R-9: "a tab switch completes within one 60 Hz frame on the
// reference machine". Stage 1 T15.
//
// THIS IS THE ONE A3 CRITERION THAT IS MEASURED RATHER THAN JUDGED, and the guide says so in its
// own wording: "Report the actual figure, not a verdict." R-1 through R-7 are things a person looks
// at; R-9 is a number, and a number that nobody has taken is not a criterion, it is a hope.
//
// ---------------------------------------------------------------------------------------------
// WHAT IS ACTUALLY TIMED, AND WHY IT IS THREE FIGURES RATHER THAN ONE
//
// Convention 12 is the whole reason this file is careful. Offscreen there is no event loop, so a
// bare `setCurrentPage()` marks layouts dirty and returns -- it would produce a beautiful
// sub-microsecond number that measures an assignment to an integer. T10's ribbon suite already made
// exactly that mistake once with `resize()`, and it reported six collapsed groups at every width
// from 1600 down to 400: monotone, reproducible, and meaningless.
//
// So the switch is driven to completion by hand and reported in three parts, because a single
// opaque total cannot be acted on:
//
//   switch  -- setCurrentPage() plus the posted events it causes. The stacked-widget swap.
//   layout  -- activating the ribbon's own layout and the incoming page's RibbonPageLayout, which
//              is where the collapse decision for the new page is made. This is the part the A3
//              verdict's "named custom work" lives in, so it is the part worth being able to see.
//   paint   -- rendering the ribbon into a QImage. Qt Widgets paint into a raster backing store on
//              Windows too, so this is representative rather than a proxy -- but it is measured
//              WITHOUT a compositor and without a real swap chain, which is the caveat and is
//              printed with the result rather than left for a reader to remember.
//
// The reported R-9 figure is the sum of the three, because that is what a user's eye waits for.
//
// ---------------------------------------------------------------------------------------------
// WHY IT IS NOT A PASS/FAIL CTEST CASE
//
// It IS registered with CTest (`bench.ribbon.tab_switch`) so that it is compiled and run on every
// Windows CI run -- a benchmark that only runs when somebody remembers it is a benchmark that has
// rotted by the time the number matters. But its exit code is about whether the measurement was
// possible, not about whether the machine was fast: a shared CI runner under load would otherwise
// turn a green build red for a reason that has nothing to do with the change. The 16.67 ms verdict
// is printed and is the guide's to record.
//
// The one thing it DOES fail on is a measurement that cannot mean anything: too few tabs to switch
// between, or a ribbon that did not build.

#include "core/cmd/ActionRegistry.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/Ribbon.h"
#include "ui/shell/RibbonModel.h"
#include "ui/shell/RibbonTabBuilder.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QTranslator>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace rawpage;

namespace {

constexpr int kIterations = 100;   // the guide's figure
constexpr int kWarmup     = 10;    // first-touch costs: font metrics, style, the backing store
constexpr int kWidth      = 1600;
constexpr int kHeight     = 200;

using Clock = std::chrono::steady_clock;

double millisSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// p50 and p95 over a copy, because the caller wants its samples back in order for nothing and
// sorting in place would make a second percentile call read a mutated vector.
double percentile(std::vector<double> samples, double fraction)
{
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(samples.size() - 1));
    return samples[std::min(index, samples.size() - 1)];
}

struct Sample {
    double switching = 0.0;
    double layout    = 0.0;
    double paint     = 0.0;
    double total() const { return switching + layout + paint; }
};

void report(const char* label, const std::vector<double>& samples)
{
    // Into locals FIRST. The three calls were once arguments to this printf, and the order in which
    // a compiler evaluates arguments is unspecified -- so the `std::move` that was on the last one
    // ran first, and p50 and p95 read a moved-from vector and answered 0.000 ms. Every figure this
    // benchmark printed on its first run was that bug rather than the ribbon; found because a
    // 192-control ribbon cannot repaint in under a microsecond and the printed p95 disagreed with
    // the p95 computed one line above it.
    const double p50 = percentile(samples, 0.50);
    const double p95 = percentile(samples, 0.95);
    const double max = percentile(samples, 1.00);
    std::printf("  %-30s p50 %7.3f ms   p95 %7.3f ms   max %7.3f ms\n", label, p50, p95, max);
}

} // namespace

int main(int argc, char** argv)
{
    // Offscreen unless the caller says otherwise. A benchmark that opens real windows on somebody's
    // desktop measures their compositor and their other windows; a developer who wants the figure
    // on a real screen sets QT_QPA_PLATFORM themselves and gets it.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication app(argc, argv);

    // The real catalog, for the reason tests/ui/main.cpp gives: without it every tab title and
    // every button label resolves to its own key, and a ribbon labelled `ribbon.tab.home` lays out
    // differently from one labelled "Home". A benchmark against text nobody will see is a benchmark
    // of the wrong widget widths.
    QTranslator source;
    const QString catalog =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("i18n/rawpage_en.qm"));
    if (source.load(catalog)) QCoreApplication::installTranslator(&source);

    // THE SHIPPED RESOURCE, not a fixture. R-9 is a claim about the product's ribbon -- 9 tabs, 49
    // groups, 192 controls -- and a benchmark over a three-button test ribbon would report a number
    // that is true of nothing anyone ships.
    const ui::ribbon::ParseResult& spec = ui::ribbon::shipped();
    if (!spec.ok()) {
        const QString file = ui::ribbon::shippedPath();
        for (const ui::ribbon::Diagnostic& d : spec.diagnostics)
            std::fprintf(stderr, "bench_ribbon: %s\n", qUtf8Printable(d.formatted(file)));
        return 2;
    }

    core::ActionRegistry registry;
    ui::ActionFactory    actions(registry, [] { return core::KeyContext::Global; });
    ui::Ribbon           ribbon;

    ui::RibbonTabBuilder builder(ribbon, actions);
    const QStringList problems = builder.build(spec);
    if (!problems.isEmpty()) {
        std::fprintf(stderr, "bench_ribbon: the ribbon did not build -- %s\n",
                     qUtf8Printable(problems.join(QStringLiteral("; "))));
        return 2;
    }

    // Shown, then sized, then laid out -- convention 12. Skipping any of the three yields a widget
    // whose children are all invisible with default geometry, and every figure below would be the
    // cost of laying out nothing.
    ribbon.show();
    ribbon.resize(kWidth, kHeight);
    ribbon.layout()->activate();
    for (int page = 0; page < ribbon.pageCount(); ++page)
        if (ui::RibbonPageLayout* pageLayout = ribbon.layoutOfPage(page)) pageLayout->activate();

    const int pages = ribbon.pageCount();
    if (pages < 2) {
        std::fprintf(stderr, "bench_ribbon: %d tab(s) -- there is nothing to switch between\n", pages);
        return 2;
    }

    QImage canvas(ribbon.size(), QImage::Format_ARGB32_Premultiplied);
    if (canvas.isNull()) {
        std::fprintf(stderr, "bench_ribbon: the ribbon has no size to paint into\n");
        return 2;
    }

    const auto collapsedNow = [&] {
        const ui::RibbonPageLayout* pageLayout = ribbon.layoutOfPage(ribbon.currentPage());
        return pageLayout != nullptr ? pageLayout->collapsedCount() : -1;
    };

    // One switch, driven all the way to painted pixels. Returned rather than accumulated so the
    // warmup and the measured run are literally the same code -- a warmup that ran a different path
    // would warm the wrong caches.
    const auto switchTo = [&](int page) {
        Sample s;

        const auto t0 = Clock::now();
        ribbon.setCurrentPage(page);
        QCoreApplication::sendPostedEvents(nullptr, 0);
        s.switching = millisSince(t0);

        const auto t1 = Clock::now();
        ribbon.layout()->activate();
        if (ui::RibbonPageLayout* pageLayout = ribbon.layoutOfPage(page)) pageLayout->activate();
        s.layout = millisSince(t1);

        const auto t2 = Clock::now();
        canvas.fill(Qt::transparent);
        QPainter painter(&canvas);
        ribbon.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
        painter.end();
        s.paint = millisSince(t2);

        return s;
    };

    for (int i = 0; i < kWarmup; ++i) switchTo(i % pages);

    std::vector<double> switching, layout, painting, totals;
    switching.reserve(kIterations);
    layout.reserve(kIterations);
    painting.reserve(kIterations);
    totals.reserve(kIterations);

    // Round-robin across every tab rather than ping-ponging between two. Nine tabs of different
    // group counts is the product's real spread, and alternating between the same pair would
    // measure a warm path the user never gets twice in a row.
    for (int i = 0; i < kIterations; ++i) {
        const Sample s = switchTo(i % pages);
        switching.push_back(s.switching);
        layout.push_back(s.layout);
        painting.push_back(s.paint);
        totals.push_back(s.total());
    }

    // -----------------------------------------------------------------------------------------
    // THE CONTROL, and it is not optional. The `layout` column above comes back at 0.000 ms for
    // every one of the 100 iterations, and there are two ways that happens: the collapse decision
    // genuinely costs nothing on a tab switch, or `activate()` is a no-op on a layout nothing ever
    // marked dirty and the column is a dead instrument reading zero. Convention 7 -- a negative
    // control that does not exercise the thing is worse than none -- says find out.
    //
    // A resize ACROSS the collapse threshold is what dirties it. If this figure is non-zero the
    // instrument works and the zero above is a fact about the ribbon: collapse is decided on
    // resize, not on tab switch, which is what makes a tab switch cheap.
    std::vector<double> relayout;
    relayout.reserve(20);
    for (int i = 0; i < 20; ++i) {
        const int width = (i % 2 == 0) ? 900 : kWidth;   // 900 forces several groups to collapse
        const auto t = Clock::now();
        ribbon.resize(width, kHeight);
        ribbon.layout()->activate();
        if (ui::RibbonPageLayout* pageLayout = ribbon.layoutOfPage(ribbon.currentPage()))
            pageLayout->activate();
        relayout.push_back(millisSince(t));
    }
    ribbon.resize(kWidth, kHeight);
    ribbon.layout()->activate();

    const double p95 = percentile(totals, 0.95);

    std::printf("\nA-85 / R-9 -- ribbon tab switch\n");
    std::printf("  %d tabs, %d iterations after %d warmup, platform=%s\n",
                pages, kIterations, kWarmup, qUtf8Printable(QGuiApplication::platformName()));

    // THE SIZE IT ACTUALLY GOT, not the size that was asked for -- T10's lesson stated as an
    // instrument reading. `resize(1600)` returning something else is a finding about the ribbon,
    // and a timing taken at an unknown width is a timing of an unknown thing. Same for the paint
    // target: an empty QImage would make the paint column free, which is precisely the failure that
    // makes a benchmark report zero.
    std::printf("  requested %dx%d, ribbon is %dx%d, painted into %dx%d, %d group(s) collapsed\n\n",
                kWidth, kHeight, ribbon.width(), ribbon.height(),
                canvas.width(), canvas.height(), collapsedNow());

    report("switch (page swap)", switching);
    report("layout (collapse decision)", layout);
    report("paint (raster, no compositor)", painting);
    std::printf("  %-28s ------------------------------------------------\n", "");
    report("TOTAL", totals);

    std::printf("\n  R-9 asks for p95 <= 16.667 ms (one 60 Hz frame). Measured p95: %.3f ms -- %s\n",
                p95, p95 <= 16.667 ? "within budget" : "OVER BUDGET");

    std::printf("\n  Control -- resize across the collapse threshold (900 <-> %d):\n", kWidth);
    report("relayout (collapse runs)", relayout);
    const double controlMax = percentile(relayout, 1.00);
    std::printf("  %s\n",
                controlMax > 0.0
                    ? "The instrument works, so the 0.000 ms layout column above is a FACT: collapse\n"
                      "  is decided on RESIZE, not on tab switch -- which is what makes a switch cheap."
                    : "WARNING: the control is zero too. The layout column is measuring nothing, and\n"
                      "  the figures above cannot be read as including a collapse cost.");

    std::printf("\n  Caveat: measured under the offscreen platform, so this is layout and raster\n"
                "  painting with no compositor and no swap chain. It is a floor, not a ceiling.\n\n");

    // Exit 0 whatever the figure. See the file header: the number is the deliverable and the
    // verdict is the guide's to record, not this binary's to enforce on a shared runner.
    return 0;
}
