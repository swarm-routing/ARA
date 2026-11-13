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

void TestBufferHoldsDuringDiscovery(Checker& c) {
    Config on;
    on.bufferDuringDiscovery = true;
    Harness s(1, on);
    const auto first = s.router.onLocalData(9);
    c.check(first.action == RouteAction::Queue && first.nextHop == 9 && s.router.discovering(9),
            "no route, buffering on: held for the destination, and a discovery starts");
    c.check(s.router.holds(9) && !s.router.holds(5), "held only while its discovery runs");
    c.check(s.router.onLocalData(9).action == RouteAction::Queue && s.out.size() == 1,
            "the next packet is held too, no second FANT");
    s.router.onAnt(bant(9, 1, 1, 2), 2);
    c.check(!s.router.holds(9) && s.router.nextHop(9) == 2,
            "the BANT ends the discovery and lays the trail the held data takes");
    const auto data = s.router.onLocalData(9);
    c.check(data.action == RouteAction::Unicast && data.nextHop == 2, "then data goes out");

    Harness off(1);
    c.check(off.router.onLocalData(9).action == RouteAction::Drop && !off.router.holds(9),
            "off (the default): dropped, nothing held");
    off.events.advance(2.81);
    c.check(off.out.size() == 1, "off: a discovery that gives up sends nothing more");
}

void TestBufferGivesUpWithTheDiscovery(Checker& c) {
    Config on;
    on.bufferDuringDiscovery = true;
    Harness s(1, on);
    s.router.onLocalData(9);
    s.events.advance(2.81);  // no BANT, no data since the FANT: the discovery ends
    c.check(!s.router.holds(9) && s.out.size() == 2, "a FANT, then one more decision");
    if (s.out.size() == 2) {
        const auto& d = s.out[1].decision;
        c.check(d.action == RouteAction::DiscardPending && d.nextHop == 9 && !d.message,
                "ended with no trail: the held data goes (DiscardPending)");
    }

    Harness t(1, on);
    t.router.onLocalData(9);
    t.router.onAnt(fant(9, 4, 1, 0), 3);  // 9's own FANT lays a trail toward 9
    t.events.advance(2.81);
    bool discarded = false;
    for (const auto& sent : t.out)
        discarded = discarded || sent.decision.action == RouteAction::DiscardPending;
    c.check(!t.router.holds(9) && t.router.nextHop(9) == 3 && !discarded,
            "ended with a trail: nothing discarded");

    Config full = on;
    full.maxDiscoveries = 0;
    Harness u(1, full);
    c.check(u.router.onLocalData(9).action == RouteAction::Drop && !u.router.holds(9),
            "no discovery can start (cap): dropped, not held");
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

// Fidelity "Ant and data dedup history size": an entry expires 30 s after it
// is recorded (#4), read against the clock on access. Far fewer than 64
// entries are recorded here, so the count cap never evicts.
void TestHistoryLifetime(Checker& c) {
    Config cfg;
    c.check(cfg.historyLifetime == 30.0, "the decided lifetime is 30 s (#4)");
    Harness h(5, cfg);
    const DataKey key{1, 9, 17, 100};
    h.router.onAnt(fant(1, 9, 7, 0), 2);
    h.router.onAnt(bant(9, 1, 7, 0), 6);
    c.check(h.router.admitData(key, 2), "t = 0: the FANT, its BANT and a data key are new");

    h.events.runUntil(29.999);
    h.out.clear();
    h.router.onAnt(fant(1, 9, 7, 0), 3);
    c.check(h.rng.draws() == 1, "just before 30 s the FANT is still a duplicate");
    h.router.onAnt(bant(9, 1, 7, 0), 6);
    c.check(h.out.empty(), "...and so is the BANT");
    c.check(!h.router.admitData(key, 3), "...and the data key");

    h.events.runUntil(30.0);
    h.router.onAnt(fant(1, 9, 7, 0), 3);
    c.check(h.rng.draws() == 2, "at 30 s the FANT entry has expired: accepted again");
    h.router.onAnt(bant(9, 1, 7, 0), 6);
    c.check(h.out.size() == 1 && h.out[0].decision.nextHop == 3,
            "...the BANT too, back over the reverse pointer recorded afresh");
    c.check(h.router.admitData(key, 3), "...and the data key");

    // The hops kept with the entries expire with them.
    const DataKey later{1, 9, 17, 101};
    h.router.onAnt(fant(1, 9, 8, 0), 3);
    h.router.admitData(later, 3);
    h.events.runUntil(60.0);
    h.out.clear();
    h.router.onAnt(bant(9, 1, 8, 0), 6);
    c.check(h.out.empty(), "a reverse pointer 30 s old is gone: the BANT goes no further");
    const FailureDecision d = h.router.onRouteFailure(later, 6);
    c.check(d.decision.action == RouteAction::Drop && !d.backtrack,
            "a previous hop 30 s old is gone: no backtrack to it");
}

}  // namespace ara::core::test
