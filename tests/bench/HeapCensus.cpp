#include "tests/bench/HeapCensus.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace rawpage::bench {

HeapCensus heapCensus()
{
    // Reserved once and reused: the walk must not allocate, or it measures itself.
    static std::vector<std::size_t>* sizes = [] {
        auto* reserved = new std::vector<std::size_t>;
        reserved->reserve(4'000'000);
        return reserved;
    }();
    sizes->clear();

    HeapCensus census;
    HANDLE heaps[256];
    const DWORD count = GetProcessHeaps(256, heaps);
    for (DWORD i = 0; i < count && i < 256; ++i) {
        if (!HeapLock(heaps[i])) continue;
        PROCESS_HEAP_ENTRY entry{};
        while (HeapWalk(heaps[i], &entry)) {
            if (entry.wFlags & PROCESS_HEAP_REGION) census.heapCommittedBytes += entry.Region.dwCommittedSize;
            // A large allocation lives outside the regions and is committed on its own.
            if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) && entry.cbData >= 512 * 1024)
                census.heapCommittedBytes += entry.cbData;
            if (!(entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)) continue;
            census.busyBytes += entry.cbData;
            census.busyBlocks += 1;
            if (sizes->size() < sizes->capacity()) sizes->push_back(entry.cbData);
        }
        HeapUnlock(heaps[i]);
    }

    MEMORY_BASIC_INFORMATION region{};
    for (LPVOID at = nullptr; VirtualQuery(at, &region, sizeof(region)) == sizeof(region);) {
        if (region.State == MEM_COMMIT && region.Type == MEM_PRIVATE) census.privateCommittedBytes += region.RegionSize;
        const auto next = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (next <= reinterpret_cast<std::uintptr_t>(at)) break;
        at = reinterpret_cast<LPVOID>(next);
    }

    std::sort(sizes->begin(), sizes->end());
    for (std::size_t at = 0; at < sizes->size();) {
        std::size_t end = at;
        while (end < sizes->size() && (*sizes)[end] == (*sizes)[at]) ++end;
        census.bySize.emplace_back((*sizes)[at], static_cast<long long>(end - at));
        at = end;
    }
    return census;
}

void printGrowth(const HeapCensus& before, const HeapCensus& after, int rounds, std::size_t limit)
{
    struct Growth {
        std::size_t size;
        long long   blocks;
        long long   bytes;
    };
    std::vector<Growth> growth;
    std::size_t b = 0;
    for (const auto& [size, blocks] : after.bySize) {
        while (b < before.bySize.size() && before.bySize[b].first < size) ++b;
        const long long had =
            (b < before.bySize.size() && before.bySize[b].first == size) ? before.bySize[b].second : 0;
        if (blocks > had)
            growth.push_back({size, blocks - had, (blocks - had) * static_cast<long long>(size)});
    }
    std::sort(growth.begin(), growth.end(),
              [](const Growth& x, const Growth& y) { return x.bytes > y.bytes; });
    std::printf("size,blocksAdded,bytesAdded,blocksPerRound\n");
    for (std::size_t i = 0; i < growth.size() && i < limit; ++i)
        std::printf("%zu,%lld,%lld,%.1f\n", growth[i].size, growth[i].blocks, growth[i].bytes,
                    rounds > 0 ? static_cast<double>(growth[i].blocks) / rounds : 0.0);
}

} // namespace rawpage::bench
