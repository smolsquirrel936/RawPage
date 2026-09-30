// PDFium's global state, owned in exactly one place.
//
// ---------------------------------------------------------------------------------------------
// WHY THIS FILE IS THE ONLY ONE THAT MAY TOUCH IT (decision D2-9, Stage2-Rendering-Plan.md 3).
//
// `FPDF_InitLibraryWithConfig` and `FPDF_DestroyLibrary` are process-wide. Called twice, or called
// from a worker while another worker is rendering, they are a crash with no useful stack. The design
// is: the library is initialized ONCE, before any worker exists, and every rasterizer is
// thread-confined to its own worker afterwards -- and making one translation unit the sole owner of
// the global half is what turns that from a convention into something a person can check.
//
// So this header declares no PDFium type, and `PdfiumInit.cpp` is the only translation unit in the
// project that includes `fpdfview.h` for the library's lifetime calls. `tools/layer-lint`'s `fpdf`
// rule is narrowed to `pdf/render/` and has a companion rule for the handle typedefs, with a fixture
// and a control for each -- prose in a header comment is not a rule; the gate is.
//
// ---------------------------------------------------------------------------------------------
// WHAT `ensure()` GUARANTEES, AND WHAT IT DOES NOT.
//
// It is idempotent and thread-safe: the first caller initializes, every later caller returns the
// same answer, and concurrent first calls do not race. It does NOT make PDFium thread-safe -- it
// makes the *initialization* safe, which is a different and smaller claim. Two workers rendering
// through one `FPDF_DOCUMENT` is still a data race; the product's answer to that is one document
// per worker, which is `PdfiumDocument`'s business at T9 and not this file's.
//
// ---------------------------------------------------------------------------------------------
// TEARDOWN IS DELIBERATELY NOT AUTOMATIC.
//
// There is no destructor here that calls `FPDF_DestroyLibrary`, and no static object arranging for
// one. A static destructor runs during process exit in an order nothing controls, after worker
// threads may have been suspended mid-render by the OS -- which is how a clean shutdown turns into a
// crash report from inside the engine. `shutdown()` exists for the one caller that genuinely wants
// it (a test that measures init being entered once), and the product simply lets the process end.
// That is not laziness: releasing an allocator's arenas moments before the kernel reclaims the whole
// address space buys nothing and can only go wrong.
//
// ---------------------------------------------------------------------------------------------
// AT STAGE 2 T2, THE ONLY CALLER IS THE TEST SUITE -- said out loud rather than left to be found.
//
// Nothing in the product initializes PDFium yet, because nothing opens a document yet:
// `pdf::createBackend()` still returns the backend that implements none of the seven interfaces
// (T1). T9 gives this its first product caller, in `PdfiumDocument::open`. Stage 1 was caught five
// times by a capability that was real, tested and reachable only from a test (its I-34, I-42, I-43,
// I-46, I-47), and the lesson recorded from it was not "do not build it early" -- it was "say which
// callers exist". This one's are `tests/render/tst_pdfiuminit.cpp` and nothing else.

#ifndef RAWPAGE_PDF_RENDER_PDFIUMINIT_H
#define RAWPAGE_PDF_RENDER_PDFIUMINIT_H

#include "core/Result.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include <QLatin1String>
#include <QString>

namespace rawpage::pdf::render {

// Initializes PDFium if it has not been initialized, and reports whether the library is usable.
// Idempotent, thread-safe, and cheap after the first call.
//
// Returns `IoFailure` only if the engine reports that it could not start. There is no "PDFium is
// missing" branch: the DLL is implicitly linked, so a process whose `pdfium.dll` cannot be found
// never reaches `main()` at all -- and that is a better failure than a lazy load that half-works.
core::Status ensure();

// Whether `ensure()` has run successfully in this process. For a diagnostic and for the case that
// asserts initialization happens once; not a precondition anybody needs to test before calling.
bool isInitialized();

// How many times the library has actually been initialized -- which must never exceed one, however
// many threads asked. The counter exists so that "entered once" is a fact a test can assert rather
// than a property nobody measures (guide case A-45's first half).
int initializationCount();

// Releases PDFium's global state. Almost nothing should call this; see the note above. Safe to call
// when it was never initialized, and safe to call twice.
void shutdown();

// THE ENGINE LOCK -- one mutex for the whole process, held across every call into PDFium.
//
// It lives here because this file is *the one place PDFium's global state is touched*, and a
// process-wide serialization lock is exactly that. Two locks would serialize nothing, which is why
// it is vended rather than declared wherever it is needed.
//
// **D2-9 said "thread-confined per worker", and that was MEASURED FALSE at T3 (issue I-05).** What
// PDFium shares between threads is process-global -- its font mapper and its glyph caches -- so
// eight workers with a document and a page each corrupt the heap, and two are enough. Confining a
// document to a worker confines the wrong thing.
//
// This is the conservative half of the choice **T11** still has to make (one render thread with a
// queue, or N workers serialized here) and is correct under either outcome. The cost was measured
// rather than feared: eight workers behind this lock retire 1.05x the tiles per second of one, and
// per-tile p95 stays at 0.532 ms against a 16.667 ms budget.
//
// HOLD IT ACROSS THE WHOLE ENGINE INTERACTION, not just the render call: `FPDFBitmap_Create` and
// `_Destroy` race on PDFium's allocator too, which is what the T3 probe crashed inside.
//
// **IT KNOWS WHO HOLDS IT** since Stage 5 T3 (Stage 5 guide issue **I-01**). `PdfiumDocument::open`
// had read the outline, the named destinations and the Info dictionary through PDFium with NO lock
// since Stage 4 T4 -- on the UI thread, while the render worker was inside the engine -- and nothing
// could see it: a missing lock is an absence, and the race it opens corrupts the heap only
// sometimes. So the mutex records its owner, and every facade reader that is not visibly inside a
// `lock_guard` calls `requireEngineLock()`, which COUNTS an entry made without it. The count is a
// fact a test asserts is zero (`tests/render/tst_pdfiumdoc.cpp`), rather than a rule a reviewer has
// to notice being broken.
//
// **AND IT IS FAIR -- first come, first served -- since Stage 5 T3.** `std::mutex` promises no
// order, and on this toolchain a thread that releases it and asks again at once wins it back
// ahead of a thread that was already waiting: measured by `bench_pipeline --stage5`, where a
// document's open, walking 900 pages and releasing the lock between each, kept a viewport tile
// waiting 60-104 ms for a lock it needed for 0.4 ms -- per-page release and a `yield()` between
// pages changed nothing. A ticket lock hands the engine to whoever asked first, so a thread that
// takes it in short pieces lets the render worker in between them.
class EngineMutex {
public:
    void lock();
    void unlock();
    bool try_lock();
    bool heldByCurrentThread() const noexcept;

private:
    std::mutex state_;
    std::condition_variable turn_;
    std::uint64_t nextTicket_ = 0;
    std::uint64_t serving_ = 0;
    std::atomic<std::thread::id> owner_{};
};

EngineMutex& engineLock();

// Counts one entry into PDFium made by a thread that does not hold `engineLock()`. Call it at the
// top of a reader whose caller is supposed to hold the lock.
void requireEngineLock() noexcept;

// How many such entries there have been in this process. Zero is the only correct value.
long long unlockedEngineEntries() noexcept;

// The pinned version, as a string: "153.0.8009.0".
//
// IT IS THE BUILD-TIME PIN, NOT SOMETHING THE DLL REPORTS. The `FPDF_` C API exposes no version
// call, so this is the value `cmake/PdfiumAssert.cmake` verified against `third_party/pdfium/VERSION`
// at configure time and handed to the compiler. That distinction matters for exactly one reader --
// somebody who suspects the `pdfium.dll` beside the executable was replaced after the build -- and
// they are the reader most likely to be looking at this line, so `--version` says so too.
QLatin1String version();

} // namespace rawpage::pdf::render

#endif // RAWPAGE_PDF_RENDER_PDFIUMINIT_H
