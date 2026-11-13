// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Loop prevention and route failure ([G02] §3.2, §3.3): duplicate data and
// DUPLICATE_ERROR, another link on a failure, backtracking to the previous
// hop, and a new discovery at the source (fidelity rows "Loop prevention",
// "On a failed link", "No other link"; #15 decided backtracking).
#include "ara-core-tests.h"

#include <cstdint>

namespace ara::core::test {

namespace {

using swarm::core::kInvalidAddress;
using swarm::core::RouteAction;

/// A FANT from `source` arriving over `from` lays the trail (source, from).
void trail(Harness& h, NodeAddress source, NodeAddress from, std::uint32_t seq,
           std::uint8_t hops = 0) {
    h.router.onAnt(AntMessage{AntType::Forward, source, 9, seq, hops, 35}, from);
}

}  // namespace

void TestDuplicateData(Checker& c) {
    Harness h(5);
    const DataKey key{1, 9, 17, 100};
    c.check(h.router.admitData(key, 2), "a data packet seen first is admitted");
    c.check(!h.router.admitData(key, 3), "the same key again is a loop: a duplicate");
    c.check(h.router.admitData(DataKey{1, 9, 17, 101}, 2) &&
                h.router.admitData(DataKey{1, 9, 6, 100}, 2) &&
                h.router.admitData(DataKey{1, 8, 17, 100}, 2) &&
                h.router.admitData(DataKey{2, 9, 17, 100}, 2),
            "every field of the key counts: id, protocol, destination, source");

    trail(h, 1, 2, 1);
    trail(h, 1, 3, 2, 1);
    c.check(h.router.nextHop(1) == 2, "(1, 2) is the stronger trail");
    h.router.onDuplicateError(1, 2);
    c.check(h.router.pheromone(1, 2) == 0.0 && h.router.nextHop(1) == 3,
            "DUPLICATE_ERROR from 2: that link's ϕ goes to 0, the other trail remains");
}

void TestFailureTakesAnotherLink(Checker& c) {
    Harness h(5);
    trail(h, 1, 2, 1);
    trail(h, 1, 3, 2, 1);
    const DataKey key{9, 1, 17, 7};
    h.router.admitData(key, 6);
    const FailureDecision d = h.router.onRouteFailure(key, 2);
    c.check(d.decision.action == RouteAction::Unicast && d.decision.nextHop == 3 && !d.backtrack,
            "the failed link is dropped and the packet takes the other trail");
    c.check(h.router.pheromone(1, 2) == 0.0, "the failed link's ϕ is 0");
}

void TestFailureBacktracks(Checker& c) {
    Harness h(5);
    trail(h, 1, 2, 1);
    const DataKey key{9, 1, 17, 7};
    h.router.admitData(key, 6);
    const FailureDecision d = h.router.onRouteFailure(key, 2);
    c.check(d.decision.action == RouteAction::Unicast && d.decision.nextHop == 6 && d.backtrack,
            "no other trail: back to the hop the packet came from, flagged");
    c.check(!h.router.discovering(1), "a relay starts no discovery");

    const FailureDecision lost = h.router.onRouteFailure(DataKey{9, 1, 17, 8}, 2);
    c.check(lost.decision.action == RouteAction::Drop && !lost.backtrack,
            "a relay that does not know the previous hop drops it");

    Config small;
    small.historyCapacity = 2;
    Harness s(5, small);
    trail(s, 1, 2, 1);
    s.router.admitData(DataKey{9, 1, 17, 1}, 6);
    s.router.admitData(DataKey{9, 1, 17, 2}, 6);
    s.router.admitData(DataKey{9, 1, 17, 3}, 6);
    c.check(s.router.onRouteFailure(DataKey{9, 1, 17, 1}, 2).decision.action == RouteAction::Drop,
            "the history is bounded: the oldest previous hop was evicted");
    c.check(s.router.admitData(DataKey{9, 1, 17, 1}, 6), "…and its key is new again");
}

void TestFailureAtSourceRediscovers(Checker& c) {
    Harness h(5);
    trail(h, 1, 2, 1);
    const std::size_t before = h.out.size();
    const DataKey key{5, 1, 17, 7};
    h.router.admitData(key, kInvalidAddress);
    const FailureDecision d = h.router.onRouteFailure(key, 2);
    c.check(d.decision.action == RouteAction::Drop && !d.backtrack,
            "at the source with no other trail: dropped (no buffering, #4)");
    c.check(h.router.discovering(1) && h.out.size() == before + 1 &&
                h.out.back().decision.action == RouteAction::Broadcast,
            "…and a new discovery starts: one FANT");
}

void TestBacktrackOnADiamond(Checker& c) {
    Network net(1);
    net.addLink(1, 2);
    net.addLink(1, 3);
    net.addLink(2, 4);
    net.addLink(3, 4);
    net.node(1).onLocalData(4);
    net.events().runUntil(1.0);
    const NodeAddress relay = net.node(1).nextHop(4);
    c.check(relay == 2 || relay == 3, "a trail from 1 toward 4");
    const DataKey key{1, 4, 17, 1};
    net.node(1).admitData(key, kInvalidAddress);
    net.node(relay).admitData(key, 1);
    const FailureDecision atRelay = net.node(relay).onRouteFailure(key, 4);
    c.check(atRelay.backtrack && atRelay.decision.nextHop == 1,
            "the relay's only trail failed: it backtracks to the source");
    const int fants = net.sent(AntType::Forward);
    const FailureDecision atSource = net.node(1).onRouteFailure(key, relay);
    const NodeAddress other = relay == 2 ? 3 : 2;
    if (net.node(1).pheromone(4, other) > 0.0) {
        c.check(atSource.decision.nextHop == other && !atSource.backtrack,
                "the source takes its other trail");
    } else {
        c.check(atSource.decision.action == RouteAction::Drop &&
                    net.sent(AntType::Forward) == fants + 1,
                "the source had no other trail: a new FANT");
        net.events().runUntil(2.0);
        c.check(net.node(1).nextHop(4) != kInvalidAddress, "…which finds a route again");
    }
}

}  // namespace ara::core::test
