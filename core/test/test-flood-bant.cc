// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Flooded BANTs (#136, the reading accepted 2026-11-24): with
// Config::floodBackwardAnts on, the BANT goes back "flooded from the target to
// the source node similar to the FANT" ([GS03] §4.5): the destination
// broadcasts it, every node relays it once per discovery, jittered, and lays
// the trail toward the destination; off, it is unicast along the FANT's path.
#include "ara-core-tests.h"

#include <cstdint>

namespace ara::core::test {

namespace {

using swarm::core::kInvalidAddress;
using swarm::core::RouteAction;

Config flooding() {
    Config c;
    c.floodBackwardAnts = true;
    return c;
}

AntMessage fantMsg(NodeAddress src, NodeAddress dst, std::uint32_t seq, std::uint8_t hops,
                   std::uint8_t ttl = 35) {
    return {AntType::Forward, src, dst, seq, hops, ttl};
}

AntMessage bantMsg(NodeAddress src, NodeAddress dst, std::uint32_t seq, std::uint8_t hops,
                   std::uint8_t ttl = 35) {
    return {AntType::Backward, src, dst, seq, hops, ttl};
}

/// Builds the diamond 1 - {2, 3} - 4 in `n` and runs a discovery from 1 for 4
/// to quiet. `n` is the caller's: a Network must not move, since its routers
/// hold references to its clock, scheduler and RNG.
void runDiamond(Network& n) {
    n.addLink(1, 2);
    n.addLink(1, 3);
    n.addLink(2, 4);
    n.addLink(3, 4);
    n.node(1).onLocalData(4);
    n.events().runUntil(1.0);
}

}  // namespace

void TestFloodedBantOnADiamond(Checker& c) {
    Network on(1, flooding());
    runDiamond(on);
    c.check(on.node(2).pheromone(4, 4) > 0.0 && on.node(3).pheromone(4, 4) > 0.0,
            "switch on: both relays lay a trail toward the destination");
    c.check(!on.node(1).discovering(4) && on.node(1).nextHop(4) != kInvalidAddress,
            "the source ends its discovery with a trail");
    // The destination's broadcast, one relay each by 2 and 3; the source,
    // the BANT's end, relays nothing, and the destination drops the echoes.
    c.check(on.sent(AntType::Backward) == 3, "one BANT per node but the source");

    Network off(1, Config{});
    runDiamond(off);
    const int trails =
        (off.node(2).pheromone(4, 4) > 0.0 ? 1 : 0) + (off.node(3).pheromone(4, 4) > 0.0 ? 1 : 0);
    c.check(trails == 1, "switch off: only the FANT's reverse-path relay lays it");
    c.check(off.sent(AntType::Backward) == 2, "switch off: the destination's and one relay's");
}

void TestFloodedBantRelay(Checker& c) {
    Harness h(5, flooding());
    h.router.onAnt(fantMsg(1, 9, 7, 0), 1);
    h.events.advance(0.010);
    h.out.clear();
    const int before = h.rng.draws();
    h.router.onAnt(bantMsg(9, 1, 7, 1), 6);
    c.check(h.router.pheromone(9, 6) == 0.5, "the trail toward the destination: 1/hops");
    c.check(h.out.empty() && h.rng.draws() == before + 1, "relayed after a jitter draw");
    h.events.advance(0.010);
    c.check(h.out.size() == 1, "one relay");
    if (h.out.size() == 1) {
        const auto& d = h.out[0].decision;
        c.check(d.action == RouteAction::Broadcast, "a broadcast, not the reverse pointer");
        c.check(d.message && *d.message == bantMsg(9, 1, 7, 2, 34), "hops + 1, ttl - 1");
    }
    h.router.onAnt(bantMsg(9, 1, 7, 1), 2);
    h.events.advance(0.010);
    c.check(h.out.size() == 1 && h.router.pheromone(9, 2) == 0.0,
            "a duplicate BANT changes nothing");
    h.router.onAnt(bantMsg(9, 1, 8, 0), 2);
    h.events.advance(0.010);
    c.check(h.out.size() == 2 && h.router.pheromone(9, 2) > 0.99,
            "no reverse pointer needed: a new discovery's BANT is relayed too");
    h.router.onAnt(bantMsg(9, 1, 9, 0, 1), 3);
    h.events.advance(0.010);
    c.check(h.out.size() == 2 && h.router.pheromone(9, 3) > 0.99,
            "TTL 1: the trail is laid, the flood stops");

    // The destination answers a FANT with a broadcast BANT, unjittered.
    Harness d(9, flooding());
    d.router.onAnt(fantMsg(1, 9, 3, 1), 4);
    c.check(d.out.size() == 1 && d.out[0].decision.action == RouteAction::Broadcast &&
                d.out[0].decision.message == bantMsg(9, 1, 3, 0),
            "the destination broadcasts its BANT at once");

    // The FANT's originator ends its discovery and relays nothing.
    Harness s(1, flooding());
    s.router.onLocalData(9);
    s.out.clear();
    s.router.onAnt(bantMsg(9, 1, 1, 2), 2);
    s.events.advance(0.010);
    c.check(!s.router.discovering(9) && s.out.empty(), "the originator ends it, relays nothing");
}

}  // namespace ara::core::test
