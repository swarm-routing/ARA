// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Statistical path selection (#135, the reading accepted 2026-11-24): with
// Config::statisticalPathSelection on, data takes neighbour j with probability
// ϕ(d, j) / Σ ϕ(d, k) ([GS03] §2.2, §5.2), the previous hop left out while
// another trail exists; off, the decisions and the draws are today's.
#include "ara-core-tests.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace ara::core::test {

namespace {

using swarm::core::kInvalidAddress;
using swarm::core::RouteAction;

Config statistical() {
    Config c;
    c.statisticalPathSelection = true;
    return c;
}

/// A FANT from 1 arriving over `from` with `hops` lays (1, from) at
/// ϕ = 1 / (hops + 1).
void trail(Harness& h, NodeAddress from, std::uint32_t seq, std::uint8_t hops) {
    h.router.onAnt(AntMessage{AntType::Forward, 1, 9, seq, hops, 35}, from);
}

/// Trails toward 1 over 2, 3 and 4 with ϕ 1, 0.5 and 0.25.
void threeTrails(Harness& h) {
    trail(h, 2, 1, 0);
    trail(h, 3, 2, 1);
    trail(h, 4, 3, 3);
}

NodeAddress hopOf(const AntDecision& d) {
    return d.action == RouteAction::Unicast ? d.nextHop : kInvalidAddress;
}

}  // namespace

void TestStatisticalShares(Checker& c) {
    Harness h(5, statistical());
    threeTrails(h);
    c.check(h.router.pheromone(1, 2) == 1.0 && h.router.pheromone(1, 3) == 0.5 &&
                h.router.pheromone(1, 4) == 0.25,
            "the trails are laid at 1, 0.5 and 0.25");
    constexpr int kDecisions = 10000;
    const int before = h.rng.draws();
    std::map<NodeAddress, int> count;
    for (int i = 0; i < kDecisions; ++i)
        ++count[hopOf(h.router.onTransitData(1, 6))];
    c.check(h.rng.draws() - before == kDecisions, "one draw per decision with three trails");
    const double sum = 1.0 + 0.5 + 0.25;
    const std::map<NodeAddress, double> expected{{2, 1.0 / sum}, {3, 0.5 / sum}, {4, 0.25 / sum}};
    for (const auto& [hop, share] : expected) {
        const double seen = static_cast<double>(count[hop]) / kDecisions;
        c.check(std::abs(seen - share) < 0.02, "each trail's share is ϕ/Σϕ within ±2 %");
    }
    c.check(count.size() == 3, "only the three trails are ever chosen");

    // The source (no previous hop) draws over every trail alike.
    std::map<NodeAddress, int> local;
    for (int i = 0; i < 3000; ++i)
        ++local[hopOf(h.router.onLocalData(1))];
    c.check(local.size() == 3, "the source's own data also draws over every trail");
}

void TestStatisticalLeavesOutThePreviousHop(Checker& c) {
    Harness h(5, statistical());
    trail(h, 2, 1, 0);
    trail(h, 3, 2, 3);
    bool onlyThree = true;
    const int before = h.rng.draws();
    for (int i = 0; i < 1000; ++i)
        onlyThree = onlyThree && hopOf(h.router.onTransitData(1, 2)) == 3;
    c.check(onlyThree, "the previous hop is never chosen while another trail exists");
    c.check(h.rng.draws() == before, "one candidate left: nothing is drawn");

    Harness one(5, statistical());
    trail(one, 2, 1, 0);
    const int drawn = one.rng.draws();
    c.check(hopOf(one.router.onTransitData(1, 2)) == 2,
            "the previous hop is chosen when it is the only trail");
    c.check(hopOf(one.router.onTransitData(1, 6)) == 2, "a single trail is taken");
    c.check(one.rng.draws() == drawn, "a single trail: nothing is drawn");
    c.check(one.router.onTransitData(7, 2).action == RouteAction::Drop,
            "no trail: Drop, as without the switch");
}

void TestStatisticalOffIsUnchanged(Checker& c) {
    Harness off(5);
    threeTrails(off);
    const int before = off.rng.draws();
    bool best = true;
    for (int i = 0; i < 1000; ++i) {
        best = best && hopOf(off.router.onTransitData(1, 2)) == 2 &&
               hopOf(off.router.onTransitData(1)) == 2 && hopOf(off.router.onLocalData(1)) == 2;
    }
    c.check(best, "switch off: the highest ϕ, the previous hop included, as before");
    c.check(off.rng.draws() == before, "switch off: no data decision draws");

    // With the switch on, the deterministic paths stay deterministic.
    Harness on(5, statistical());
    threeTrails(on);
    const int drawn = on.rng.draws();
    c.check(on.router.nextHop(1) == 2, "nextHop() stays the highest ϕ");
    const DataKey key{9, 1, 17, 7};
    on.router.admitData(key, 6);
    const FailureDecision f = on.router.onRouteFailure(key, 2);
    c.check(hopOf(f.decision) == 3 && !f.backtrack,
            "the route-failure alternative stays the highest remaining ϕ");
    c.check(on.rng.draws() == drawn, "nextHop() and onRouteFailure draw nothing");
}

void TestStatisticalDeterminism(Checker& c) {
    auto run = [](std::uint64_t seed) {
        Harness h(5, statistical(), seed);
        threeTrails(h);
        std::vector<NodeAddress> hops;
        for (int i = 0; i < 200; ++i)
            hops.push_back(hopOf(h.router.onTransitData(1, 6)));
        return hops;
    };
    c.check(run(7) == run(7), "the same seed gives the same choices");
    c.check(run(7) != run(8), "another seed gives other choices");
}

}  // namespace ara::core::test
