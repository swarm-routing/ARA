// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The per-event cost of pheromone decay, eager against lazyDecay (#50). A
// router holds `dests` x `nbrs` trails; each event is one data packet sent to
// a destination drawn at random (onDataSent, then nextHop: both age before
// they read), 1 ms of simulated time apart. Prints the median of five runs of
// nanoseconds per event for each table size. Wall-clock time is the measure
// here, so nothing it prints is an output of the protocol (org rule 5).
// Built with -DARA_CORE_BENCH=ON; not run by CI.
#include "../test/ara-test-support.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace {

using ara::core::AntMessage;
using ara::core::AntType;
using ara::core::Config;
using ara::core::NodeAddress;
using ara::core::test::Harness;

constexpr NodeAddress kFirstNeighbour = 50000;

double nsPerEvent(int dests, int nbrs, bool lazy, int events) {
    Config c;
    c.lazyDecay = lazy;
    // The default capacity, or twice the table when it does not fit: no write
    // is refused, so no reclaim sweep runs (the core tests cover that path).
    c.pheromoneCapacity =
        std::max(Config{}.pheromoneCapacity, static_cast<std::size_t>(2 * dests * nbrs));
    Harness h(100000, c);
    std::uint32_t seq = 1;
    for (int d = 0; d < dests; ++d) {
        for (int n = 0; n < nbrs; ++n)
            h.router.onAnt(AntMessage{AntType::Forward, 1 + d, 99, seq++, 0, 35},
                           kFirstNeighbour + n);
    }
    std::uint64_t x = 88172645463325252ULL;  // xorshift64: the same draws every run
    NodeAddress seen = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int e = 0; e < events; ++e) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        h.events.advance(0.001);
        const auto d = static_cast<NodeAddress>(1 + x % static_cast<std::uint64_t>(dests));
        const auto n = static_cast<NodeAddress>((x >> 20) % static_cast<std::uint64_t>(nbrs));
        h.router.onDataSent(d, kFirstNeighbour + n);
        seen ^= h.router.nextHop(d);
    }
    const auto t1 = std::chrono::steady_clock::now();
    if (seen == -1) std::puts("");  // keeps the reads
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / events;
}

double median5(int dests, int nbrs, bool lazy, int events) {
    std::array<double, 5> r{};
    for (double& v : r)
        v = nsPerEvent(dests, nbrs, lazy, events);
    std::sort(r.begin(), r.end());
    return r[2];
}

}  // namespace

int main() {
    std::printf("dests x nbrs   eager ns/event   lazy ns/event   eager/lazy\n");
    for (const auto& [d, n] : {std::pair{4, 4}, std::pair{16, 8}, std::pair{64, 16},
                               std::pair{256, 16}, std::pair{1024, 16}}) {
        const double eager = median5(d, n, false, 200000);
        const double lazy = median5(d, n, true, 200000);
        std::printf("%5d x %-4d   %14.1f   %13.1f   %9.1fx\n", d, n, eager, lazy, eager / lazy);
    }
    return 0;
}
