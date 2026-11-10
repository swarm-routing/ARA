// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// One router at a time: each rule of route discovery against its
// docs/fidelity.md row, with the emissions recorded and the RNG seeded.
#include "ara-core-tests.h"

#include <cmath>
#include <cstdint>

namespace ara::core::test {

namespace {

using swarm::core::kInvalidAddress;
using swarm::core::RouteAction;

bool near(double a, double b, double tol = 1e-9) {
    return std::fabs(a - b) <= tol;
}

AntMessage fant(NodeAddress src, NodeAddress dst, std::uint32_t seq, std::uint8_t hops,
                std::uint8_t ttl = 35) {
    return {AntType::Forward, src, dst, seq, hops, ttl};
}

AntMessage bant(NodeAddress src, NodeAddress dst, std::uint32_t seq, std::uint8_t hops,
                std::uint8_t ttl = 35) {
    return {AntType::Backward, src, dst, seq, hops, ttl};
}

}  // namespace

void TestFantRelay(Checker& c) {
    Harness h(5);
    h.router.onAnt(fant(1, 9, 7, 2), 3);
    c.check(near(h.router.pheromone(1, 3), 1.0 / 3.0), "trail toward the source: 1/hops");
    c.check(h.out.empty() && h.rng.draws() == 1, "the relay waits for its jitter, one draw");
    h.events.advance(0.010);
    c.check(h.out.size() == 1, "relayed once within the 10 ms jitter");
    if (h.out.size() != 1) return;
    const auto& d = h.out[0].decision;
    c.check(d.action == RouteAction::Broadcast, "the FANT is flooded");
    c.check(d.message && *d.message == fant(1, 9, 7, 3, 34), "relayed with hops + 1, ttl - 1");
    c.check(h.out[0].at >= 0.0 && h.out[0].at < 0.010, "jitter in [0, 10 ms)");
}

void TestFantDuplicatesAndTtl(Checker& c) {
    Harness h(5);
    h.router.onAnt(fant(1, 9, 7, 0), 3);
    c.check(near(h.router.pheromone(1, 3), 1.0), "a one-hop FANT lays 1");
    h.router.onAnt(fant(1, 9, 7, 0), 4);
    c.check(h.router.pheromone(1, 4) == 0.0 && h.rng.draws() == 1,
            "a duplicate (source, seq) changes nothing and is not relayed");
    h.router.onAnt(fant(1, 9, 8, 3), 3);
    c.check(near(h.router.pheromone(1, 3), 1.0), "a longer FANT never lowers a trail");
    h.router.onAnt(fant(1, 9, 9, 0, 1), 6);
    c.check(near(h.router.pheromone(1, 6), 1.0) && h.rng.draws() == 2,
            "ttl 1: the trail is laid, the FANT is not relayed");
    h.router.onAnt(fant(5, 9, 1, 2), 3);
    c.check(h.router.pheromone(5, 3) == 0.0 && h.rng.draws() == 2,
            "this node's own FANT is ignored");
    h.events.advance(1.0);
    c.check(h.out.size() == 2, "two relays: seq 7 and seq 8");
}

void TestDestinationAnswers(Checker& c) {
    Harness h(9);
    h.router.onAnt(fant(1, 9, 7, 2), 3);
    c.check(near(h.router.pheromone(1, 3), 1.0 / 3.0), "the destination lays the trail too");
    c.check(h.rng.draws() == 0, "the destination does not relay the FANT");
    c.check(h.out.size() == 1, "one BANT, sent at once");
    if (h.out.size() != 1) return;
    const auto& d = h.out[0].decision;
    c.check(d.action == RouteAction::Unicast && d.nextHop == 3,
            "unicast to the FANT's previous hop");
    c.check(d.message && *d.message == bant(9, 1, 7, 0), "BANT: from the destination, same seq");
    h.router.onAnt(fant(1, 9, 7, 1), 4);
    c.check(h.out.size() == 1, "a duplicate FANT gets no second BANT");
}

void TestBantRelay(Checker& c) {
    Harness h(5);
    h.router.onAnt(fant(1, 9, 7, 0), 1);
    h.events.advance(0.010);
    h.out.clear();
    h.router.onAnt(bant(9, 1, 7, 1), 6);
    c.check(near(h.router.pheromone(9, 6), 0.5), "trail toward the destination: 1/hops");
    c.check(h.out.size() == 1, "relayed at once, no jitter");
    if (h.out.size() == 1) {
        const auto& d = h.out[0].decision;
        c.check(d.action == RouteAction::Unicast && d.nextHop == 1, "back along the FANT's path");
        c.check(d.message && *d.message == bant(9, 1, 7, 2, 34), "hops + 1, ttl - 1");
    }
    h.router.onAnt(bant(9, 1, 7, 1), 6);
    c.check(h.out.size() == 1, "a duplicate BANT is dropped");
    h.router.onAnt(bant(9, 1, 8, 0), 2);
    c.check(h.out.size() == 1 && near(h.router.pheromone(9, 2), 1.0),
            "no reverse pointer: the trail is laid, the BANT goes no further");

    Harness s(1);
    const auto first = s.router.onLocalData(9);
    c.check(first.action == RouteAction::Drop && s.router.discovering(9),
            "no route: drop, discover");
    c.check(s.out.size() == 1 && s.out[0].decision.action == RouteAction::Broadcast &&
                s.out[0].decision.message == fant(1, 9, 1, 0),
            "the source floods a FANT, unjittered");
    s.router.onAnt(fant(1, 9, 1, 2), 2);
    c.check(s.rng.draws() == 0, "the source drops the echo of its own FANT");
    s.router.onAnt(bant(9, 1, 1, 2), 2);
    c.check(!s.router.discovering(9), "the BANT completes the discovery");
    const auto data = s.router.onLocalData(9);
    c.check(data.action == RouteAction::Unicast && data.nextHop == 2 && !data.message,
            "data goes out after the BANT");
    c.check(s.router.onLocalData(1).action == RouteAction::Deliver,
            "data for this node is delivered");
}

void TestEvaporation(Checker& c) {
    Harness h(5);
    h.router.onAnt(fant(1, 9, 1, 0), 2);
    h.events.advance(1.0);
    c.check(near(h.router.pheromone(1, 2), 0.9), "one period: (1 - q)");
    h.events.advance(0.5);
    c.check(near(h.router.pheromone(1, 2), 0.9 * std::pow(0.9, 0.5)), "continuous between periods");
    h.events.advance(42.0);  // 0.9^43.5 = 0.0103: still above 0.01
    c.check(h.router.nextHop(1) == 2, "above the expiry threshold the trail stays");
    h.events.advance(1.0);  // 0.9^44.5 = 0.0093
    c.check(h.router.pheromone(1, 2) == 0.0 && h.router.nextHop(1) == kInvalidAddress,
            "below 0.01 the entry is gone");
}

void TestNextHopChoice(Checker& c) {
    Harness h(5);
    c.check(h.router.nextHop(1) == kInvalidAddress, "no trail, no next hop");
    h.router.onAnt(fant(1, 9, 1, 1), 3);
    h.router.onAnt(fant(1, 9, 2, 1), 2);
    c.check(h.router.nextHop(1) == 2, "a tie goes to the lowest address");
    h.router.onAnt(fant(1, 9, 3, 0), 4);
    c.check(h.router.nextHop(1) == 4, "the highest pheromone wins, deterministically");
    const int draws = h.rng.draws();
    for (int i = 0; i < 10; ++i)
        h.router.onLocalData(1);
    c.check(h.rng.draws() == draws, "choosing a next hop draws nothing");
}

void TestRetryOnlyWithDemand(Checker& c) {
    Harness h(1);
    h.router.onLocalData(9);
    h.events.advance(2.81);
    c.check(h.out.size() == 1 && !h.router.discovering(9),
            "no data since the FANT: no retry, the discovery ends");

    h.router.onLocalData(9);
    c.check(h.out.size() == 2 && h.router.discovering(9), "the next packet starts a new discovery");
    h.events.advance(1.0);
    h.router.onLocalData(9);  // the source keeps sending
    h.events.advance(1.81);
    c.check(h.out.size() == 3 && h.router.discovering(9), "data since the FANT: one retry");
    if (h.out.size() == 3) {
        c.check(h.out[2].decision.message->sequence == 3, "a retry is a new FANT (new sequence)");
    }
}

void TestRetryLimitAndDoubling(Checker& c) {
    Harness h(1);
    h.router.onLocalData(9);  // t = 0
    for (Time t : {1.0, 3.0, 9.0, 12.0}) {
        h.events.runUntil(t);
        h.router.onLocalData(9);  // demand before every timeout
    }
    h.events.runUntil(19.5);
    c.check(h.router.discovering(9), "still waiting after the second retry");
    h.events.runUntil(19.7);
    c.check(!h.router.discovering(9), "two retries at most: the discovery ends");
    c.check(h.out.size() == 3, "the first FANT and two retries");
    if (h.out.size() == 3) {
        c.check(near(h.out[1].at, 2.8) && near(h.out[2].at, 8.4),
                "timeouts of 2.8 s, then 5.6 s (doubled)");
    }
    h.router.onLocalData(9);
    c.check(h.out.size() == 4, "later data starts a fresh discovery");
}

void TestHistoryCap(Checker& c) {
    Config cfg;
    c.check(cfg.historyCapacity == 64, "the decided cap is 64 (#5)");
    Harness h(5, cfg);
    for (std::uint32_t seq = 1; seq <= 65; ++seq)
        h.router.onAnt(fant(1, 9, seq, 0), 2);
    c.check(h.rng.draws() == 65, "65 distinct FANTs relayed");
    h.router.onAnt(fant(1, 9, 65, 0), 2);
    c.check(h.rng.draws() == 65, "the newest is still remembered");
    h.router.onAnt(fant(1, 9, 1, 0), 2);
    c.check(h.rng.draws() == 66, "the oldest was evicted, so it counts as new");
    h.events.advance(1.0);
    h.out.clear();
    h.router.onAnt(bant(9, 1, 2, 0), 7);
    c.check(h.out.empty(), "reverse pointers share the cap: seq 2's is gone");
    h.router.onAnt(bant(9, 1, 3, 0), 7);
    c.check(h.out.size() == 1 && h.out[0].decision.nextHop == 2, "seq 3's is kept");

    Config small;
    small.maxDiscoveries = 2;
    Harness s(1, small);
    s.router.onLocalData(7);
    s.router.onLocalData(8);
    s.router.onLocalData(9);
    c.check(s.out.size() == 2 && !s.router.discovering(9), "at most maxDiscoveries at once");
}

}  // namespace ara::core::test
