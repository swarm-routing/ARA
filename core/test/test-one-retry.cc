// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// One more try on a failed link (#138, the reading accepted 2026-11-24): with
// Config::oneRetryOnFailure on, a node takes one other trail per packet
// ([GS03] §4.3); a second failure of that packet there goes back to the
// previous hop, flagged, as "No other link". Off, every failure takes the next
// trail, as [G02] §3.3.
#include "ara-core-tests.h"

#include <cstdint>

namespace ara::core::test {

namespace {

using swarm::core::kInvalidAddress;
using swarm::core::RouteAction;

Config oneRetry() {
    Config c;
    c.oneRetryOnFailure = true;
    return c;
}

/// A FANT from 1 arriving over `from` with `hops` lays (1, from) at 1/(hops+1).
void trail(Harness& h, NodeAddress from, std::uint32_t seq, std::uint8_t hops) {
    h.router.onAnt(AntMessage{AntType::Forward, 1, 9, seq, hops, 35}, from);
}

/// Trails toward 1 over 2, 3 and 4 (ϕ 1, 0.5, 0.25), at relay 5.
void threeTrails(Harness& h) {
    trail(h, 2, 1, 0);
    trail(h, 3, 2, 1);
    trail(h, 4, 3, 3);
}

}  // namespace

void TestOneRetryAtARelay(Checker& c) {
    Harness on(5, oneRetry());
    threeTrails(on);
    const DataKey key{9, 1, 17, 7};
    on.router.admitData(key, 6);
    const FailureDecision first = on.router.onRouteFailure(key, 2);
    c.check(first.decision.action == RouteAction::Unicast && first.decision.nextHop == 3 &&
                !first.backtrack,
            "the first failure takes one other trail");
    const FailureDecision second = on.router.onRouteFailure(key, 3);
    c.check(second.decision.nextHop == 6 && second.backtrack,
            "the second failure tries no third trail: back to the previous hop");
    c.check(on.router.pheromone(1, 3) == 0.0 && on.router.pheromone(1, 4) > 0.0,
            "both failed links are dropped, the untried trail stays");
    const DataKey other{9, 1, 17, 8};
    on.router.admitData(other, 6);
    const FailureDecision fresh = on.router.onRouteFailure(other, kInvalidAddress);
    c.check(fresh.decision.nextHop == 4 && !fresh.backtrack, "another packet has its own try");

    Harness off(5);
    threeTrails(off);
    off.router.admitData(key, 6);
    off.router.onRouteFailure(key, 2);
    const FailureDecision again = off.router.onRouteFailure(key, 3);
    c.check(again.decision.nextHop == 4 && !again.backtrack,
            "switch off: the second failure takes the third trail, as before");
}

void TestOneRetryAtTheSourceAndEviction(Checker& c) {
    // A source's own packet has no history entry: the mark creates one.
    Harness src(1, Config{oneRetry()});
    src.router.onAnt(AntMessage{AntType::Forward, 9, 1, 1, 0, 35}, 2);
    src.router.onAnt(AntMessage{AntType::Forward, 9, 1, 2, 1, 35}, 3);
    src.router.onAnt(AntMessage{AntType::Forward, 9, 1, 3, 3, 35}, 4);
    const DataKey own{1, 9, 17, 1};
    c.check(src.router.onRouteFailure(own, 2).decision.nextHop == 3, "the source's one try");
    const FailureDecision done = src.router.onRouteFailure(own, 3);
    c.check(done.decision.action == RouteAction::Drop && !done.backtrack &&
                src.router.discovering(9),
            "its second failure: dropped and a new discovery, as with no other link");

    // An evicted entry counts as untried (fails open, as the previous hop does).
    Config small = oneRetry();
    small.historyCapacity = 2;
    Harness h(5, small);
    threeTrails(h);
    const DataKey key{9, 1, 17, 1};
    h.router.admitData(key, 6);
    h.router.onRouteFailure(key, 2);
    h.router.admitData(DataKey{9, 1, 17, 2}, 6);
    h.router.admitData(DataKey{9, 1, 17, 3}, 6);
    const FailureDecision evicted = h.router.onRouteFailure(key, 3, 6);
    c.check(evicted.decision.nextHop == 4 && !evicted.backtrack,
            "an evicted entry gets a fresh try");
}

}  // namespace ara::core::test
