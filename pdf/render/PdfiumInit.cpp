#include "pdf/render/PdfiumInit.h"

#include "core/Log.h"

#include <fpdfview.h>

#include <atomic>
#include <mutex>

#ifndef RAWPAGE_PDFIUM_VERSION
// Never reached in a configured build -- cmake/PdfiumAssert.cmake defines this from the version it
// verified. The guard exists so that a hand-run compile of this file fails with a sentence rather
// than with an undefined macro three expansions deep.
#    error "RAWPAGE_PDFIUM_VERSION is not defined -- cmake/PdfiumAssert.cmake sets it (D2-3)"
#endif

namespace rawpage::pdf::render {
namespace {

using core::LogSubsystem;

std::mutex        g_mutex;
bool              g_initialized = false;
std::atomic<int>  g_initCount{0};

} // namespace

core::Status ensure()
{
    // A lock rather than std::call_once, because `shutdown()` can put the state back and call_once
    // is a one-way latch: the flag cannot be reset, so a second `ensure()` after a `shutdown()`
    // would silently do nothing and hand every later call a library that is not there. The lock is
    // taken once per call and the whole body is a boolean test after the first time.
    const std::lock_guard<std::mutex> guard(g_mutex);
    if (g_initialized) return core::Status{};

    // Version 2 of the config struct, with the V8 fields left null. They are meaningful only in a
    // V8-enabled build, and PA-4 in cmake/PdfiumAssert.cmake asserts this is not one -- so passing
    // an isolate here would be describing an engine we deliberately did not fetch.
    FPDF_LIBRARY_CONFIG config{};
    config.version           = 2;
    config.m_pUserFontPaths  = nullptr;   // font resolution is ours, through IFontResolver (T16)
    config.m_pIsolate        = nullptr;
    config.m_v8EmbedderSlot  = 0;

    FPDF_InitLibraryWithConfig(&config);

    // PDFium's initializer returns void, so "did it work" has to be asked another way: the error
    // slot is cleared by a successful start-up, and a library that failed to come up reports it
    // here. This is thin evidence and it is the only evidence the C API offers -- which is worth
    // knowing rather than dressing up.
    const unsigned long error = FPDF_GetLastError();
    if (error != FPDF_ERR_SUCCESS) {
        RAWPAGE_LOG_ERROR(LogSubsystem::Render)
            << "pdfium: FPDF_InitLibraryWithConfig reported error" << static_cast<int>(error);
        return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.engine.initFailed"),
                           QString::number(static_cast<qulonglong>(error))};
    }

    g_initialized = true;
    g_initCount.fetch_add(1, std::memory_order_relaxed);

    RAWPAGE_LOG_INFO(LogSubsystem::Render)
        << "pdfium: initialized, version" << version() << "(V8 off, XFA off)";
    return core::Status{};
}

bool isInitialized()
{
    const std::lock_guard<std::mutex> guard(g_mutex);
    return g_initialized;
}

int initializationCount()
{
    return g_initCount.load(std::memory_order_relaxed);
}

void shutdown()
{
    const std::lock_guard<std::mutex> guard(g_mutex);
    if (!g_initialized) return;
    FPDF_DestroyLibrary();
    g_initialized = false;
    RAWPAGE_LOG_DEBUG(LogSubsystem::Render) << "pdfium: destroyed";
}

void EngineMutex::lock()
{
    std::unique_lock<std::mutex> guard(state_);
    const std::uint64_t ticket = nextTicket_++;
    turn_.wait(guard, [this, ticket] { return serving_ == ticket; });
    owner_.store(std::this_thread::get_id());
}

bool EngineMutex::try_lock()
{
    const std::lock_guard<std::mutex> guard(state_);
    if (serving_ != nextTicket_) return false;   // held, or someone is already waiting
    ++nextTicket_;
    owner_.store(std::this_thread::get_id());
    return true;
}

void EngineMutex::unlock()
{
    {
        const std::lock_guard<std::mutex> guard(state_);
        owner_.store(std::thread::id{});
        ++serving_;
    }
    // ALL, because the next ticket's holder is one particular waiter and a condition variable
    // cannot wake a chosen thread. The others re-check and sleep; with one render worker and at
    // most one other caller there are rarely others.
    turn_.notify_all();
}

bool EngineMutex::heldByCurrentThread() const noexcept
{
    return owner_.load() == std::this_thread::get_id();
}

EngineMutex& engineLock()
{
    // A function-local static, so its construction order cannot matter and it is never destroyed
    // before a worker that is still holding it. Deliberately NOT the same mutex as `g_mutex`
    // above: that one guards initialization and is taken briefly, while this one is held across a
    // whole render and would make `ensure()` wait behind an unrelated tile.
    static EngineMutex lock;
    return lock;
}

namespace {
std::atomic<long long> g_unlockedEntries{0};
}

void requireEngineLock() noexcept
{
    if (!engineLock().heldByCurrentThread()) g_unlockedEntries.fetch_add(1);
}

long long unlockedEngineEntries() noexcept
{
    return g_unlockedEntries.load();
}

QLatin1String version()
{
    return QLatin1String(RAWPAGE_PDFIUM_VERSION);
}

} // namespace rawpage::pdf::render
