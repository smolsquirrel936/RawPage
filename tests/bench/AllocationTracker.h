// AllocationTracker -- see the .cpp. Linking it into an executable replaces that executable's global
// operator new/delete; nothing is recorded until startTracking().
#ifndef RAWPAGE_TESTS_BENCH_ALLOCATIONTRACKER_H
#define RAWPAGE_TESTS_BENCH_ALLOCATIONTRACKER_H

#include <cstddef>

namespace rawpage::bench {

void startTracking();
void stopTracking();

// Prints every allocation made while tracking that has not been freed, grouped by call stack and
// sorted by count. `rounds` only scales the per-round column.
void reportSurvivors(int rounds, std::size_t limit);

} // namespace rawpage::bench

#endif // RAWPAGE_TESTS_BENCH_ALLOCATIONTRACKER_H
