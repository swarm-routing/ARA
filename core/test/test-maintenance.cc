// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Route maintenance by data ([G02] §3.2): each data hop adds Δϕ toward the
// destination at the sender and toward the source at the receiver (fidelity
// "Route maintenance by data", "Deposit Δϕ per data packet").
#include "ara-core-tests.h"

#include <cmath>
#include <cstdint>

namespace ara::core::test {

namespace {

using swarm::core::kInvalidAddress;

bool near(double a, double b, double tol = 1e-9) {
    return std::fabs(a - b) <= tol;
}

/// One data packet from `src` to `dst` along the trail: each node hands it
/// to its next hop, which receives it. Returns the hops taken, or -1 when a
/// node on the way has no trail (or the walk exceeds `maxHops`).
int sendData(Network& net, NodeAddress src, NodeAddress dst, int maxHops = 16) {
    NodeAddress at = src;
    for (int hops = 0; hops < maxHops; ++hops) {
        if (at == dst) return hops;
        const NodeAddress next = net.node(at).nextHop(dst);
        if (next == kInvalidAddress) return -1;
        net.node(at).onDataSent(dst, next);
        net.node(next).onDataReceived(src, at);
        at = next;
    }
    return -1;
}

}  // namespace

void TestDataReinforcement(Checker& c) {
    c.check(Config{}.deposit == 0.1, "the decided Δϕ is 0.1 (#4)");
    Harness h(5);
    h.router.onAnt(AntMessage{AntType::Forward, 1, 9, 7, 0, 35}, 2);  // (1, 2) at 1
    h.router.onDataSent(1, 2);
    c.check(near(h.router.pheromone(1, 2), 1.1), "the sender adds Δϕ toward the destination");
    h.router.onDataReceived(9, 6);
    c.check(near(h.router.pheromone(9, 6), 0.1) && h.router.nextHop(9) == 6,
            "the receiver adds Δϕ toward the source; a missing entry starts at Δϕ");
    h.events.advance(1.0);
    h.router.onDataSent(1, 2);
    c.check(near(h.router.pheromone(1, 2), 1.1 * 0.9 + 0.1), "evaporated to now, then Δϕ");

    h.router.onDataSent(5, 2);
    h.router.onDataReceived(5, 3);
    h.router.onDataReceived(9, kInvalidAddress);
    h.router.onDataSent(kInvalidAddress, 2);
    c.check(h.router.pheromone(5, 2) == 0.0 && h.router.pheromone(5, 3) == 0.0 &&
                near(h.router.pheromone(9, 6), 0.1 * 0.9),
            "nothing toward this node itself, nothing over an unknown neighbour");
    c.check(h.rng.draws() == 1 && h.out.size() == 1,
            "data draws nothing and sends no ant (only the FANT's relay)");

    Config one;
    one.pheromoneCapacity = 1;
    one.deposit = 0.25;
    Harness s(5, one);
    s.router.onDataReceived(9, 6);
    s.router.onDataReceived(8, 6);
    c.check(s.router.pheromone(8, 6) == 0.0, "a full table refuses a new entry (org rule 7)");
    s.router.onDataReceived(9, 6);
    c.check(near(s.router.pheromone(9, 6), 0.5),
            "an existing entry still grows, by Config::deposit");
}

void TestDataKeepsTrailsAlive(Checker& c) {
    // Discovery on 1-2-3-4 lays 1/3 at the ends; unvisited, such a trail falls
    // below 0.01 after about 33 s. One packet a second keeps both directions.
    Network kept(1);
    Network idle(1);
    for (Network* net : {&kept, &idle}) {
        net->addLink(1, 2);
        net->addLink(2, 3);
        net->addLink(3, 4);
        net->node(1).onLocalData(4);
        net->events().runUntil(1.0);
    }
    bool delivered = true;
    for (int t = 2; t <= 60; ++t) {
        kept.events().runUntil(t);
        delivered = delivered && sendData(kept, 1, 4) == 3;
    }
    idle.events().runUntil(60.0);
    c.check(delivered, "every packet followed the trail, three hops");
    c.check(kept.node(1).nextHop(4) == 2 && kept.node(4).nextHop(1) == 3,
            "data kept both trails alive past expiry");
    // ϕ := 0.9·ϕ + 0.1 per second tends to Δϕ / q = 1.
    c.check(kept.node(1).pheromone(4, 2) > 0.95 && kept.node(4).pheromone(1, 3) > 0.95,
            "toward Δϕ / q = 1 under one packet a second");
    c.check(idle.node(1).nextHop(4) == kInvalidAddress &&
                idle.node(4).nextHop(1) == kInvalidAddress,
            "without data both trails expired");
    c.check(kept.sent(AntType::Forward) == idle.sent(AntType::Forward),
            "route maintenance sends no ant ([G02] §3.2: no special packets)");
}

}  // namespace ara::core::test
