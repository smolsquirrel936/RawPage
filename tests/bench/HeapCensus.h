// HeapCensus -- what is still ALLOCATED in this process, by walking every heap. Stage 2 issue I-35's
// measuring instrument, shared by bench_openclose and bench_openclose_ui.
//
// WHY NOT PRIVATE BYTES. A heap keeps memory it has freed, so private bytes swung by ~180 MB between
// rounds in I-35's harness and hid a trend of kilobytes. The sum of BUSY blocks counts only what is
// still allocated -- by our code, by Qt's DLLs and by PDFium's, all of which allocate from the
// process's NT heaps on this toolchain (pdfium.dll carries no PartitionAlloc; checked in the DLL).
//
// The walk allocates NOTHING while it runs: sizes go into a buffer reserved once and are sorted
// afterwards. A census that grew a std::map during the walk counted its own nodes, and a histogram
// that gains a few new sizes per round then reports 48-byte "growth" that is only the instrument.
#ifndef RAWPAGE_TESTS_BENCH_HEAPCENSUS_H
#define RAWPAGE_TESTS_BENCH_HEAPCENSUS_H

#include <cstddef>
#include <utility>
#include <vector>

namespace rawpage::bench {

struct HeapCensus {
    std::size_t busyBytes = 0;
    std::size_t busyBlocks = 0;
    std::vector<std::pair<std::size_t, long long>> bySize;   // (block size, how many), sorted by size

    // WHERE PRIVATE BYTES ACTUALLY ARE. Committed private memory in the address space, split into what
    // the NT heaps have committed -- busy blocks plus freed blocks the heap is keeping -- and private
    // memory outside every heap (a VirtualAlloc: a GPU driver, a JIT, an allocator of its own).
    std::size_t heapCommittedBytes = 0;      // committed bytes of every heap region
    std::size_t privateCommittedBytes = 0;   // every MEM_PRIVATE | MEM_COMMIT region in the process
};

HeapCensus heapCensus();

// The sizes whose block count grew from `before` to `after`, largest byte growth first -- the
// column that says WHICH allocation is accumulating when the total only says that one is.
void printGrowth(const HeapCensus& before, const HeapCensus& after, int rounds, std::size_t limit);

} // namespace rawpage::bench

#endif // RAWPAGE_TESTS_BENCH_HEAPCENSUS_H
