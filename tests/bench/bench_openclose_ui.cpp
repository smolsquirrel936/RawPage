// rawpage_bench_openclose_ui -- Stage 2 issue I-35 with the WINDOW in the loop, and no person.
//
// bench_openclose measures the pipeline below ui/; the product, driven by hand through a real
// desktop, kept ~4 MB of private bytes per round of ten documents. This composes the product itself
// -- app::CompositionRoot and ui::MainWindow, as app/main.cpp does -- on Qt's offscreen platform,
// opens and closes corpus files in rounds, and reports what is still allocated after each round.
// It is how I-35's three causes were found, in the order they were found:
//
//   1. the log pane kept every line ever logged          (logPaneBlocks passes the buffer's cap)
//   2. a QToolButton gains a changed() connection per    (the connection census names the signal)
//      change of its default action
//   3. what is left is Qt's QPixmapCache, which is bounded (--clear-pixmap-cache removes it)
//
// A scratch settings file, and no log directory: a diagnostic must not write into the profile of
// whoever runs it (CompositionRoot::Options says the same).
//
// WHAT TO READ. `liveHeapMB` -- the sum of busy heap blocks -- and never `privateMB` alone: private
// bytes include memory the heap has freed and kept, and swing by ~180 MB between rounds here. After
// the last round it prints which block SIZES grew since the middle round and which SIGNALS gained
// connections, which between them turned "memory grows" into a line of code twice.
//
// Switches:
//   <N>                   rounds (default 20)
//   --only <file>         one corpus file per round instead of the nine
//   --no-view             never page or zoom; open, paint once, close
//   --with-encrypted      add encrypted/aes-256.pdf, answering its password prompt by timer
//   --bus                 open through the REAL single-instance pipe: a rawpage.exe per file forwards
//                         into this process, which holds rawpage.exe's scope (RawPage must not be running)
//   --screen <n>          with --native, put the window on monitor n (QGuiApplication::screens() order)
//   --handoff             open through MainWindow::openHandoff, the single-instance path
//   --quiet-log           logging/level = Error, so almost nothing is logged
//   --clear-pixmap-cache  clear Qt's QPixmapCache before measuring each round
//   --compact             HeapCompact every heap after each round, before measuring
//   --pump <ms>           event-loop time after each open (default 300)
//   --track <r>           record C++ allocations from round r on and print the call stacks of the
//                         survivors (AllocationTracker: our code's `new` only, not Qt's or PDFium's)
//   --dump-size <bytes>   print the first bytes of live heap blocks of exactly that size

#include "app/CompositionRoot.h"
#include "app/Services.h"
#include "app/SingleInstance.h"
#include "platform/platform.h"
#include "core/LogBuffer.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderScheduler.h"
#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"
#include "tests/bench/AllocationTracker.h"
#include "tests/bench/HeapCensus.h"
#include "ui/panes/LogPane.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/PageView.h"

#include <QApplication>
#include <QDir>
#include <QDialog>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QTimer>
#include <QFileInfo>
#include <QProcess>
#include <QEvent>
#include <QGuiApplication>
#include <QInputMethod>
#include <QMetaMethod>
#include <QPixmapCache>
#include <QPlainTextEdit>
#include <QScreen>
#include <QStringList>
#include <QStyle>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QWidget>

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <limits>
#include <vector>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <set>
#include <string>

namespace {

using namespace rawpage;

double privateMB()
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters)))
        return 0.0;
    return static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
}

unsigned long handleCount()
{
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    return handles;
}

// CONNECTIONS, not objects -- a connection is not a QObject, so no object count moves when one
// leaks. `QObject::receivers()` is protected; a diagnostic subclass exposes it. Summed per
// class::signal over every object reachable from the window and the application.
struct ReceiverPeek : QObject {
    using QObject::receivers;
};

std::map<std::string, long long> connectionCensus(QWidget& window)
{
    std::set<QObject*> objects;
    const auto addTree = [&objects](QObject* root) {
        if (!root) return;
        objects.insert(root);
        for (QObject* child : root->findChildren<QObject*>()) objects.insert(child);
    };
    addTree(&window);
    addTree(QCoreApplication::instance());
    for (QWidget* widget : QApplication::allWidgets()) addTree(widget);
    for (QScreen* screen : QGuiApplication::screens()) addTree(screen);
    addTree(QGuiApplication::styleHints());
    addTree(QGuiApplication::inputMethod());

    std::map<std::string, long long> census;
    for (QObject* object : objects) {
        const QMetaObject* meta = object->metaObject();
        for (int i = 0; i < meta->methodCount(); ++i) {
            const QMetaMethod method = meta->method(i);
            if (method.methodType() != QMetaMethod::Signal) continue;
            const QByteArray signature = QByteArray("2") + method.methodSignature();
            const int count = static_cast<ReceiverPeek*>(object)->receivers(signature.constData());
            if (count > 0)
                census[std::string(meta->className()) + "::" + method.methodSignature().toStdString()] +=
                    count;
        }
    }
    return census;
}

// The first bytes of up to `limit` live heap blocks of exactly `size` bytes. I-35's 24,160-byte
// blocks were identified this way: a solid colour with anti-aliased edges is a cached pixmap.
void dumpBlocksOfSize(std::size_t size, int limit)
{
    HANDLE heaps[256];
    const DWORD count = GetProcessHeaps(256, heaps);
    int shown = 0;
    for (DWORD i = 0; i < count && i < 256 && shown < limit; ++i) {
        if (!HeapLock(heaps[i])) continue;
        PROCESS_HEAP_ENTRY entry{};
        while (shown < limit && HeapWalk(heaps[i], &entry)) {
            if (!(entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) || entry.cbData != size) continue;
            const auto* bytes = static_cast<const unsigned char*>(entry.lpData);
            std::printf("\nblock %d of %zu bytes:\n", ++shown, size);
            if (bytes[4] == 0 && size > 24) {
                // A Qt string's payload starts 24 bytes in; print it whole as UTF-16.
                std::printf("  UTF-16: ");
                for (std::size_t k = 16; k + 1 < size; k += 2) {
                    const unsigned ch = bytes[k] | (bytes[k + 1] << 8);
                    if (ch == 0) break;
                    std::printf("%c", ch >= 32 && ch < 127 ? static_cast<char>(ch) : '?');
                }
                std::printf("\n");
            }
            for (std::size_t row = 0; row < 2 && (row + 1) * 16 <= size; ++row) {
                std::printf("  ");
                for (int k = 0; k < 16; ++k) std::printf("%02x ", bytes[row * 16 + k]);
                std::printf(" ");
                for (int k = 0; k < 16; ++k) {
                    const unsigned char ch = bytes[row * 16 + k];
                    std::printf("%c", ch >= 32 && ch < 127 ? ch : '.');
                }
                std::printf("\n");
            }
        }
        HeapUnlock(heaps[i]);
    }
}

// The event loop for `milliseconds`, INCLUDING deferred deletes -- `closeDocument` uses
// `deleteLater`, and processEvents alone does not run a DeferredDelete posted at this loop level.
void pump(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    } while (timer.elapsed() < milliseconds);
}

// The M-09 driver's files, minus the encrypted one: its password prompt is modal and would stop an
// unattended loop. The other nine are the same files in the same order.
const char* const kFiles[] = {
    "minimal.pdf",
    "ordinary/text-a4.pdf",
    "ordinary/cjk-embedded.pdf",
    "ordinary/image-jpx.pdf",
    "ordinary/image-ccitt-g4.pdf",
    "ordinary/transparency-groups.pdf",
    "ordinary/pages-900.pdf",
    "malformed/xref-missing.pdf",
    "malformed/startxref-past-eof.pdf",
};

} // namespace

int main(int argc, char** argv)
{
    // Offscreen unless asked otherwise: `--native` runs on the real Windows platform, whose default
    // STYLE differs from offscreen's -- and I-35's last residual turned out to be a style's cache keys.
    bool native = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--native") == 0) native = true;
    if (!native) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    int rounds = 20;
    bool handoff = false;
    bool view = true;
    bool quietLog = false;
    bool clearPixmapCache = false;
    int pumpMs = 300;
    int trackFrom = 0;
    long long dumpSize = 0;
    QString only;
    QString experiment;
    bool withEncrypted = false;
    bool viaBus = false;
    bool compact = false;
    int screenIndex = -1;
    const QStringList arguments = QCoreApplication::arguments();
    for (int at = 1; at < arguments.size(); ++at) {
        const QString& argument = arguments.at(at);
        const bool hasValue = at + 1 < arguments.size();
        if (argument == QLatin1String("--native")) continue;
        else if (argument == QLatin1String("--handoff")) handoff = true;
        else if (argument == QLatin1String("--with-encrypted")) withEncrypted = true;
        else if (argument == QLatin1String("--bus")) viaBus = true;
        else if (argument == QLatin1String("--compact")) compact = true;
        else if (argument == QLatin1String("--screen") && hasValue) screenIndex = arguments.at(++at).toInt();
        else if (argument == QLatin1String("--no-view")) view = false;
        else if (argument == QLatin1String("--quiet-log")) quietLog = true;
        else if (argument == QLatin1String("--clear-pixmap-cache")) clearPixmapCache = true;
        else if (argument == QLatin1String("--only") && hasValue) only = arguments.at(++at);
        else if (argument == QLatin1String("--experiment") && hasValue) experiment = arguments.at(++at);
        else if (argument == QLatin1String("--pump") && hasValue) pumpMs = arguments.at(++at).toInt();
        else if (argument == QLatin1String("--track") && hasValue) trackFrom = arguments.at(++at).toInt();
        else if (argument == QLatin1String("--dump-size") && hasValue)
            dumpSize = arguments.at(++at).toLongLong();
        else {
            bool numeric = false;
            const int value = argument.toInt(&numeric);
            if (numeric && value > 0) rounds = value;
        }
    }

    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        std::fprintf(stderr, "openclose-ui: no scratch directory\n");
        return 1;
    }
    app::CompositionRoot::Options options;
    options.settingsPath = QDir(scratch.path()).filePath(QStringLiteral("settings.ini"));
    app::CompositionRoot root(std::move(options));
    if (quietLog) root.settings().set(core::keys::logging::level, core::LogLevel::Error);

    ui::MainWindow window(root.services());
    // --bus: THE REAL SINGLE-INSTANCE PIPE. This process claims the scope `rawpage.exe` computes for
    // itself, so a real `rawpage.exe -- <file>` started per open is a SECONDARY: it connects, forwards
    // and exits, and the file arrives through the bus's own listener exactly as in M-09's driven runs.
    // Without it the harness calls `openHandoff` directly and never exercises the pipe.
    const QString productExe = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("rawpage.exe"));
    std::unique_ptr<app::SingleInstance> bus;
    if (viaBus) {
        bus = std::make_unique<app::SingleInstance>(root.platform().singleInstanceBus.get(),
                                                    app::SingleInstance::scopeIdForExecutable(productExe));
        const core::Result<app::SingleInstance::Role> claimed = bus->claim(true);
        if (!claimed || claimed.value() != app::SingleInstance::Role::Primary) {
            std::fprintf(stderr, "openclose-ui: could not become the primary for %s -- is RawPage running?\n",
                         productExe.toStdString().c_str());
            return 1;
        }
        const core::Status listening =
            bus->listen([&window](const QStringList& files) { window.openHandoff(files); });
        if (!listening) {
            std::fprintf(stderr, "openclose-ui: the bus would not listen\n");
            return 1;
        }
    }
    const auto forwardThroughBus = [&productExe](const QString& file) {
        QProcess secondary;
        secondary.start(productExe, {QStringLiteral("--"), file});
        // The event loop must keep running while the secondary forwards: the listener delivers on
        // this thread, and blocking here would stall the handoff it is waiting for.
        QElapsedTimer timer;
        timer.start();
        while (secondary.state() != QProcess::NotRunning && timer.elapsed() < 15000) pump(10);
        if (secondary.state() != QProcess::NotRunning) secondary.kill();
    };
    window.resize(1920, 1080);
    if (native) {
        window.setAttribute(Qt::WA_ShowWithoutActivating);   // never take the keyboard from a person
        const QList<QScreen*> screens = QGuiApplication::screens();
        if (screenIndex >= 0 && screenIndex < screens.size()) {
            // A chosen monitor -- I-35's product runs were on the 150 % one, whose pixmaps are 2.25x.
            window.setGeometry(screens.at(screenIndex)->availableGeometry());
        } else {
            window.move(-2600, 40);
        }
    }
    window.show();
    pump(500);

    QStringList roundFiles;
    if (!only.isEmpty()) roundFiles << only;
    else for (const char* relative : kFiles) roundFiles << QString::fromLatin1(relative);
    if (withEncrypted && only.isEmpty()) roundFiles << QStringLiteral("encrypted/aes-256.pdf");

    // The password prompt is modal. A timer answers it the way a person would -- types the corpus's
    // public password into its line edit and accepts -- so the encrypted path runs unattended.
    QTimer answerer;
    answerer.setInterval(25);
    QObject::connect(&answerer, &QTimer::timeout, [] {
        QWidget* modal = QApplication::activeModalWidget();
        if (!modal || modal->objectName() != QLatin1String("passwordDialog")) return;
        if (auto* edit = modal->findChild<QLineEdit*>()) edit->setText(QStringLiteral("user"));
        if (auto* dialog = qobject_cast<QDialog*>(modal)) dialog->accept();
    });
    if (withEncrypted) answerer.start();

    const QDir corpus(QStringLiteral(RAWPAGE_CORPUS_DIR));
    std::printf("openclose-ui: %d rounds x %lld files, %s, view %s, log %s, pixmap cache %s, "
                "pump %d ms (QPixmapCache limit %d KB, style %s, platform %s)\n",
                rounds, static_cast<long long>(roundFiles.size()),
                viaBus ? "the real single-instance pipe" : handoff ? "openHandoff" : "openFiles", view ? "ON" : "off",
                quietLog ? "quiet" : "Info", clearPixmapCache ? "CLEARED each round" : "kept", pumpMs,
                QPixmapCache::cacheLimit(), QApplication::style()->name().toStdString().c_str(),
                QGuiApplication::platformName().toStdString().c_str());
    for (int i = 0; i < QGuiApplication::screens().size(); ++i)
        std::printf("screen %d: %s, dpr %.2f%s\n", i,
                    QGuiApplication::screens().at(i)->name().toStdString().c_str(),
                    QGuiApplication::screens().at(i)->devicePixelRatio(),
                    window.screen() == QGuiApplication::screens().at(i) ? "  <- window" : "");
    std::printf("round,privateMB,liveHeapMB,liveBlocks,handles,documentsOpenAtPeak,allWidgets,"
                "windowChildren,cacheEntries,pendingPlusRunning,logBuffer,logPaneBlocks,recentEntries,recentListSetting,"
                "heapCommittedMB,heapFreeKeptMB,privateOutsideHeapsMB\n");

    // --experiment: ONE operation, 200 times, with the heap census around it. Elimination rather
    // than archaeology -- each run answers whether that operation alone leaves anything behind.
    if (!experiment.isEmpty()) {
        const QString path = corpus.filePath(only.isEmpty() ? QStringLiteral("minimal.pdf") : only);
        const auto countOfSize = [](const bench::HeapCensus& census, std::size_t size) {
            for (const auto& [s, n] : census.bySize)
                if (s == size) return n;
            return 0LL;
        };
        const auto once = [&] {
            if (experiment == QLatin1String("qfileinfo")) {
                const QString absolute = QFileInfo(path).absoluteFilePath();
                (void)absolute;
            } else if (experiment == QLatin1String("backend")) {
                const core::Result<core::DocumentHandle> handle = root.backend().open(path, {});
                if (handle) root.backend().close(handle.value());
            } else if (experiment == QLatin1String("open-missing")) {
                window.openFiles({corpus.filePath(QStringLiteral("no-such-file.pdf"))});
            } else if (experiment == QLatin1String("window")) {
                window.openFiles({path});
                window.closeAllDocuments();
            } else if (experiment == QLatin1String("window-slow")) {
                window.openFiles({path});
                pump(pumpMs);
                window.closeAllDocuments();
                pump(1000);
            } else if (experiment == QLatin1String("window-slow-open")) {
                window.openFiles({path});
                pump(pumpMs);
                window.closeAllDocuments();
            } else if (experiment == QLatin1String("window-slow-close")) {
                window.openFiles({path});
                window.closeAllDocuments();
                pump(1000);
            }
            pump(20);
        };
        const int repeats = experiment.startsWith(QLatin1String("window-slow")) ? 40 : 200;
        for (int i = 0; i < 10; ++i) once();   // warm-up: first-use caches are not the question
        pump(300);
        const bench::HeapCensus before = bench::heapCensus();
        for (int i = 0; i < repeats; ++i) once();
        pump(500);
        if (clearPixmapCache) QPixmapCache::clear();
        if (compact) {
            // Asks every heap to decommit the free memory it is keeping -- the question being whether
            // private bytes that live heap does not explain are RECLAIMABLE or fragmented beyond reach.
            HANDLE heaps[256];
            const DWORD count = GetProcessHeaps(256, heaps);
            for (DWORD i = 0; i < count && i < 256; ++i) HeapCompact(heaps[i], 0);
        }
        const bench::HeapCensus after = bench::heapCensus();
        std::printf("experiment %s x%d: live heap %+.1f KB, 218-byte blocks %+lld\n",
                    experiment.toStdString().c_str(), repeats,
                    (static_cast<double>(after.busyBytes) - before.busyBytes) / 1024.0,
                    countOfSize(after, 218) - countOfSize(before, 218));
        bench::printGrowth(before, after, repeats, 8);
        return 0;
    }
    const int middle = std::max(1, rounds / 2);
    bench::HeapCensus heapAtMiddle;
    std::map<std::string, long long> connectionsAtMiddle;

    for (int round = 1; round <= rounds; ++round) {
        if (trackFrom > 0 && round == trackFrom) bench::startTracking();
        for (const QString& relative : roundFiles) {
            const QStringList files{corpus.filePath(relative)};
            if (viaBus)
                forwardThroughBus(files.first());
            else if (handoff)
                window.openHandoff(files);
            else
                window.openFiles(files);
            pump(pumpMs);
            if (!view) continue;
            if (ui::DocumentWindow* document = window.documentAt(window.documentCount() - 1)) {
                if (ui::PageView* page = document->view()) {
                    page->goToPage(2);
                    pump(pumpMs / 3);
                    page->zoomIn();
                    page->zoomIn();
                    pump(pumpMs / 3);
                    page->zoomOut();
                    pump(pumpMs / 3);
                }
            }
        }
        const int openAtPeak = window.documentCount();
        window.closeAllDocuments();
        pump(1000);
        if (clearPixmapCache) QPixmapCache::clear();
        if (compact) {
            // Asks every heap to decommit the free memory it is keeping -- the question being whether
            // private bytes that live heap does not explain are RECLAIMABLE or fragmented beyond reach.
            HANDLE heaps[256];
            const DWORD count = GetProcessHeaps(256, heaps);
            for (DWORD i = 0; i < count && i < 256; ++i) HeapCompact(heaps[i], 0);
        }

        int logPaneBlocks = -1;
        if (auto* pane = window.findChild<ui::LogPane*>())
            if (auto* text = pane->findChild<QPlainTextEdit*>()) logPaneBlocks = text->blockCount();

        // At the middle round the connection census runs BEFORE the heap census, and at the last
        // round AFTER it, so neither census's own maps are counted as growth.
        if (round == middle) connectionsAtMiddle = connectionCensus(window);
        const bench::HeapCensus heap = bench::heapCensus();
        if (round == middle) heapAtMiddle = heap;

        std::printf("%d,%.1f,%.2f,%zu,%lu,%d,%lld,%lld,%zu,%zu,%zu,%d,%zu,%lld,%.1f,%.1f,%.1f\n", round, privateMB(),
                    heap.busyBytes / (1024.0 * 1024.0), heap.busyBlocks, handleCount(), openAtPeak,
                    static_cast<long long>(QApplication::allWidgets().size()),
                    static_cast<long long>(window.findChildren<QObject*>().size()),
                    root.renderCache().entryCount(),
                    root.renderScheduler().pendingCount() + root.renderScheduler().runningCount(),
                    root.logBuffer().size(), logPaneBlocks, root.recentFiles().entries().size(),
                    static_cast<long long>(root.settings().get(core::keys::documents::recentList).size()),
                    heap.heapCommittedBytes / (1024.0 * 1024.0),
                    (static_cast<double>(heap.heapCommittedBytes) - heap.busyBytes) / (1024.0 * 1024.0),
                    (static_cast<double>(heap.privateCommittedBytes) - heap.heapCommittedBytes) / (1024.0 * 1024.0));
        std::fflush(stdout);

        if (round == rounds && rounds > middle) {
            std::printf("\nlive heap %+.1f KB per round from round %d to %d; block sizes that grew:\n",
                        (static_cast<double>(heap.busyBytes) - heapAtMiddle.busyBytes) / 1024.0
                            / (rounds - middle),
                        middle, rounds);
            bench::printGrowth(heapAtMiddle, heap, rounds - middle, 15);

            const std::map<std::string, long long> connections = connectionCensus(window);
            std::printf("\nsignals whose connection count changed from round %d to %d:\n", middle,
                        rounds);
            bool any = false;
            for (const auto& [name, count] : connections) {
                const auto before = connectionsAtMiddle.find(name);
                const long long delta =
                    count - (before == connectionsAtMiddle.end() ? 0 : before->second);
                if (delta == 0) continue;
                std::printf("  %+lld  %s (now %lld)\n", delta, name.c_str(), count);
                any = true;
            }
            if (!any) std::printf("  none\n");
        }
    }

    if (trackFrom > 0 && trackFrom <= rounds) bench::reportSurvivors(rounds - trackFrom + 1, 20);
    if (dumpSize > 0) {
        dumpBlocksOfSize(static_cast<std::size_t>(dumpSize), 100000);
        // WHO HOLDS THEM: every busy block containing a pointer INTO one of the newest blocks of that
        // size, with the holder's size and the pointer's offset -- a container reveals itself by both.
        std::vector<std::pair<std::uintptr_t, std::uintptr_t>> targets;
        {
            HANDLE heaps[256];
            const DWORD count = GetProcessHeaps(256, heaps);
            for (DWORD i = 0; i < count && i < 256; ++i) {
                if (!HeapLock(heaps[i])) continue;
                PROCESS_HEAP_ENTRY entry{};
                while (HeapWalk(heaps[i], &entry))
                    if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
                        && entry.cbData == static_cast<std::size_t>(dumpSize))
                        targets.emplace_back(reinterpret_cast<std::uintptr_t>(entry.lpData),
                                             reinterpret_cast<std::uintptr_t>(entry.lpData) + entry.cbData);
                HeapUnlock(heaps[i]);
            }
        }
        std::sort(targets.begin(), targets.end());
        std::printf("\n%zu blocks of %lld bytes; holders of pointers into them:\n", targets.size(), dumpSize);
        std::map<std::pair<std::size_t, std::size_t>, int> holders;   // (holder size, offset) -> count
        HANDLE heaps[256];
        const DWORD count = GetProcessHeaps(256, heaps);
        for (DWORD i = 0; i < count && i < 256; ++i) {
            if (!HeapLock(heaps[i])) continue;
            PROCESS_HEAP_ENTRY entry{};
            while (HeapWalk(heaps[i], &entry)) {
                if (!(entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) || entry.cbData < 8) continue;
                const auto* words = static_cast<const std::uintptr_t*>(entry.lpData);
                for (std::size_t k = 0; k + 8 <= entry.cbData; k += 8) {
                    const std::uintptr_t value = words[k / 8];
                    auto it = std::upper_bound(targets.begin(), targets.end(),
                                               std::make_pair(value, std::numeric_limits<std::uintptr_t>::max()));
                    if (it == targets.begin()) continue;
                    --it;
                    if (value >= it->first && value < it->second) holders[{entry.cbData, k}] += 1;
                }
            }
            HeapUnlock(heaps[i]);
        }
        // Summarise holders by size, then go ONE LEVEL UP for the busiest: which blocks point at
        // those holder arrays, and what their first words are -- a first word inside a module is
        // usually a vtable, and a vtable names a class.
        std::map<std::size_t, int> perHolderSize;
        for (const auto& [key, n] : holders) perHolderSize[key.first] += n;
        for (const auto& [size, n] : perHolderSize)
            std::printf("  %d pointer(s) held in %zu-byte blocks\n", n, size);

        std::size_t busiestSize = 0;
        int busiestCount = 0;
        for (const auto& [size, n] : perHolderSize)
            if (n > busiestCount) { busiestCount = n; busiestSize = size; }
        std::vector<std::pair<std::uintptr_t, std::uintptr_t>> arrays;
        for (DWORD i = 0; i < count && i < 256; ++i) {
            if (!HeapLock(heaps[i])) continue;
            PROCESS_HEAP_ENTRY entry{};
            while (HeapWalk(heaps[i], &entry)) {
                if (!(entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) || entry.cbData != busiestSize) continue;
                const auto* words = static_cast<const std::uintptr_t*>(entry.lpData);
                int inside = 0;
                for (std::size_t k = 0; k + 8 <= entry.cbData; k += 8) {
                    const std::uintptr_t value = words[k / 8];
                    auto it = std::upper_bound(targets.begin(), targets.end(),
                                               std::make_pair(value, std::numeric_limits<std::uintptr_t>::max()));
                    if (it != targets.begin() && value >= std::prev(it)->first && value < std::prev(it)->second) ++inside;
                }
                if (inside > 4)
                    arrays.emplace_back(reinterpret_cast<std::uintptr_t>(entry.lpData),
                                        reinterpret_cast<std::uintptr_t>(entry.lpData) + entry.cbData);
            }
            HeapUnlock(heaps[i]);
        }
        std::sort(arrays.begin(), arrays.end());
        std::printf("\n%zu %zu-byte array(s) hold most of them; blocks pointing at those arrays:\n",
                    arrays.size(), busiestSize);
        const auto describeWord = [](std::uintptr_t value) {
            HMODULE module = nullptr;
            char name[MAX_PATH] = "";
            if (value > 0x10000
                && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                          | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      reinterpret_cast<LPCSTR>(value), &module)) {
                GetModuleFileNameA(module, name, MAX_PATH);
                const char* slash = std::strrchr(name, '\\');
                std::printf(" [%s+0x%llx]", slash ? slash + 1 : name,
                            static_cast<unsigned long long>(value - reinterpret_cast<std::uintptr_t>(module)));
            } else {
                std::printf(" %016llx", static_cast<unsigned long long>(value));
            }
        };
        for (DWORD i = 0; i < count && i < 256; ++i) {
            if (!HeapLock(heaps[i])) continue;
            PROCESS_HEAP_ENTRY entry{};
            while (HeapWalk(heaps[i], &entry)) {
                if (!(entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) || entry.cbData < 8) continue;
                const auto* words = static_cast<const std::uintptr_t*>(entry.lpData);
                for (std::size_t k = 0; k + 8 <= entry.cbData; k += 8) {
                    const std::uintptr_t value = words[k / 8];
                    auto it = std::upper_bound(arrays.begin(), arrays.end(),
                                               std::make_pair(value, std::numeric_limits<std::uintptr_t>::max()));
                    if (it == arrays.begin() || value < std::prev(it)->first || value >= std::prev(it)->second) continue;
                    std::printf("  %zu-byte block, pointer at offset %zu; first words:", entry.cbData, k);
                    for (std::size_t w = 0; w < 6 && (w + 1) * 8 <= entry.cbData; ++w) describeWord(words[w]);
                    std::printf("\n");
                }
            }
            HeapUnlock(heaps[i]);
        }
        // Pointers held in module data sections (statics) are not heap blocks and are not seen above.
        // And pointers held OUTSIDE the heap -- globals or statics in a module -- are not seen here.
    }
    return 0;
}
