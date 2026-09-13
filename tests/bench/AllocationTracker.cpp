// AllocationTracker -- which C++ allocations made while tracking is on are still alive when it is
// asked, grouped by the call stack that made them. Stage 2 issue I-35's instrument.
//
// It replaces the global `operator new`/`operator delete` OF THE EXECUTABLE IT IS LINKED INTO, which
// is what makes it useful here: `rawpage_core`, `rawpage_ui`, `rawpage_pdfium` and `rawpage_cos` are
// static libraries, so every `new` in our code -- including the slot objects a lambda `connect()`
// instantiates in the caller -- goes through these. Allocations made inside Qt's or PDFium's own
// DLLs do NOT, and a clean report says nothing about them.
//
// Addresses are printed as RVAs -- offsets into their module. `addr2line -e <module>` wants the VMA,
// which is the RVA plus the ImageBase `objdump -p <module>` reports from the file on disk.

#include "tests/bench/AllocationTracker.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kFrames = 14;

struct Record {
    std::size_t size = 0;
    int frames = 0;
    void* stack[kFrames] = {};
};

// Plain malloc-backed allocator for the tracker's own containers, so recording an allocation does
// not recurse into the hook it is recording from.
template <typename T>
struct RawAllocator {
    using value_type = T;
    RawAllocator() = default;
    template <typename U> RawAllocator(const RawAllocator<U>&) {}
    T* allocate(std::size_t n) { return static_cast<T*>(std::malloc(n * sizeof(T))); }
    void deallocate(T* p, std::size_t) { std::free(p); }
    template <typename U> bool operator==(const RawAllocator<U>&) const { return true; }
    template <typename U> bool operator!=(const RawAllocator<U>&) const { return false; }
};

using LiveMap = std::unordered_map<void*, Record, std::hash<void*>, std::equal_to<void*>,
                                   RawAllocator<std::pair<void* const, Record>>>;

std::atomic<bool> g_tracking{false};
std::mutex& trackerMutex()
{
    static std::mutex* mutex = new (std::malloc(sizeof(std::mutex))) std::mutex;
    return *mutex;
}
LiveMap& live()
{
    static LiveMap* map = new (std::malloc(sizeof(LiveMap))) LiveMap;
    return *map;
}
thread_local bool t_inHook = false;

void* allocate(std::size_t size)
{
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    if (g_tracking.load(std::memory_order_relaxed) && !t_inHook) {
        t_inHook = true;
        Record record;
        record.size = size;
        record.frames = RtlCaptureStackBackTrace(1, kFrames, record.stack, nullptr);
        {
            std::lock_guard<std::mutex> lock(trackerMutex());
            live()[p] = record;
        }
        t_inHook = false;
    }
    return p;
}

void release(void* p) noexcept
{
    if (!p) return;
    if (!t_inHook) {
        t_inHook = true;
        {
            std::lock_guard<std::mutex> lock(trackerMutex());
            live().erase(p);
        }
        t_inHook = false;
    }
    std::free(p);
}

struct ModuleInfo {
    char name[MAX_PATH] = {};
    std::uintptr_t base = 0;
    std::uintptr_t preferred = 0;
};

ModuleInfo moduleOf(void* address)
{
    ModuleInfo info;
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCSTR>(address), &module))
        return info;
    GetModuleFileNameA(module, info.name, MAX_PATH);
    info.base = reinterpret_cast<std::uintptr_t>(module);
    const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(
        reinterpret_cast<std::uintptr_t>(module) + static_cast<std::uintptr_t>(dos->e_lfanew));
    info.preferred = static_cast<std::uintptr_t>(nt->OptionalHeader.ImageBase);
    return info;
}

} // namespace

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate(size); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { release(p); }

namespace rawpage::bench {

void startTracking()
{
    {
        std::lock_guard<std::mutex> lock(trackerMutex());
        live().clear();
    }
    g_tracking.store(true);
}

void stopTracking() { g_tracking.store(false); }

void reportSurvivors(int rounds, std::size_t limit)
{
    const bool wasTracking = g_tracking.exchange(false);
    t_inHook = true;

    struct Group {
        long long count = 0;
        long long bytes = 0;
        std::vector<void*> stack;
    };
    std::map<std::vector<void*>, Group> groups;
    {
        std::lock_guard<std::mutex> lock(trackerMutex());
        for (const auto& [pointer, record] : live()) {
            (void)pointer;
            std::vector<void*> key(record.stack, record.stack + record.frames);
            Group& group = groups[key];
            group.count += 1;
            group.bytes += static_cast<long long>(record.size);
            group.stack = key;
        }
    }
    std::vector<Group> sorted;
    for (auto& [key, group] : groups) {
        (void)key;
        sorted.push_back(group);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const Group& a, const Group& b) { return a.count > b.count; });

    std::printf("\nsurviving C++ allocations made while tracking, by call stack (%zu stacks):\n",
                sorted.size());
    for (std::size_t i = 0; i < sorted.size() && i < limit; ++i) {
        const Group& group = sorted[i];
        std::printf("#%zu  %lld allocations (%.1f per round), %lld bytes\n", i + 1, group.count,
                    rounds > 0 ? static_cast<double>(group.count) / rounds : 0.0, group.bytes);
        for (void* frame : group.stack) {
            const ModuleInfo module = moduleOf(frame);
            const char* slash = std::max(std::strrchr(module.name, '\\'), std::strrchr(module.name, '/'));
            // The RVA. The loader rewrites the in-memory ImageBase to wherever ASLR put the image, so
            // the preferred base has to come from the FILE: add `objdump -p <module>`'s ImageBase.
            const std::uintptr_t rva = reinterpret_cast<std::uintptr_t>(frame) - module.base;
            std::printf("    %s +0x%llx\n", slash ? slash + 1 : module.name,
                        static_cast<unsigned long long>(rva));
        }
    }
    std::fflush(stdout);
    t_inHook = false;
    if (wasTracking) g_tracking.store(true);
}

} // namespace rawpage::bench
