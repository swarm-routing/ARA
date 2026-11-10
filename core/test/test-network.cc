// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Routers talking to each other through the codec: a discovery end to end,
// and the determinism of the whole exchange under a seeded RNG (org rule 5).
#include "ara-core-tests.h"

#include <cmath>
#include <string>
#include <vector>

namespace ara::core::test {

namespace {

using swarm::core::kInvalidAddress;

std::vector<std::string> gridRun(std::uint64_t seed) {
    // 3 x 3 grid, nodes 0..8; two discoveries across it.
    Network net(seed);
    for (int r = 0; r < 3; ++r) {
        for (int col = 0; col < 3; ++col) {
            const int n = r * 3 + col;
            if (col < 2) net.addLink(n, n + 1);
            if (r < 2) net.addLink(n, n + 3);
        }
    }
    net.node(0).onLocalData(8);
    net.node(2).onLocalData(6);
    net.events().runUntil(1.0);
    std::vector<std::string> out = net.trace();
    for (int n = 0; n < 9; ++n) {
        out.push_back(std::to_string(n) + " next(8)=" + std::to_string(net.node(n).nextHop(8)) +
                      " next(6)=" + std::to_string(net.node(n).nextHop(6)));
    }
    return out;
}

}  // namespace

void TestDiscoveryOnALine(Checker& c) {
    Network line(1);
    line.addLink(1, 2);
    line.addLink(2, 3);
    line.addLink(3, 4);
    line.node(1).onLocalData(4);
    line.events().runUntil(1.0);
    c.check(!line.node(1).discovering(4), "the BANT reached the source");
    c.check(line.node(1).nextHop(4) == 2 && line.node(2).nextHop(4) == 3 &&
                line.node(3).nextHop(4) == 4,
            "the BANT laid the trail toward the destination");
    c.check(line.node(4).nextHop(1) == 3 && line.node(3).nextHop(1) == 2 &&
                line.node(2).nextHop(1) == 1,
            "the FANT laid the trail toward the source");
    // Laid at 1/3 (three hops) some 30 ms in, then evaporated until t = 1 s:
    // between (1/3)·0.9 and 1/3.
    const double phi = line.node(1).pheromone(4, 2);
    c.check(phi < 1.0 / 3.0 && phi > 0.9 / 3.0, "the source's trail is 1/3: three hops");
    c.check(line.sent(AntType::Forward) == 3,
            "the source and two relays flood; the destination does not");
    c.check(line.sent(AntType::Backward) == 3, "one BANT, three unicast hops");

    Network diamond(1);
    diamond.addLink(1, 2);
    diamond.addLink(1, 3);
    diamond.addLink(2, 4);
    diamond.addLink(3, 4);
    diamond.node(1).onLocalData(4);
    diamond.events().runUntil(1.0);
    c.check(diamond.sent(AntType::Backward) == 2, "the destination answers the first FANT only");
    const NodeAddress hop = diamond.node(1).nextHop(4);
    c.check((hop == 2 || hop == 3) && diamond.node(hop).nextHop(4) == 4,
            "one trail from source to destination");
    c.check(diamond.node(1).onLocalData(4).nextHop == hop, "data follows it");
    c.check(diamond.node(4).nextHop(1) != kInvalidAddress, "and the way back exists");
}

void TestDeterminism(Checker& c) {
    const auto a = gridRun(1);
    const auto b = gridRun(1);
    const auto other = gridRun(2);
    c.check(a == b, "seed 1 twice: byte-identical traces");
    c.check(a != other, "seed 2: the jitter draws differ, so does the trace");
    c.check(a.size() > 9, "the run did something");
}

}  // namespace ara::core::test
