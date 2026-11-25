// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Node lifecycle (#100): Router::reset forgets every trail, history, reverse
// pointer and discovery, as a freshly constructed router would hold, tells the
// observer each trail it dropped, and keeps counting FANT sequence numbers.
#include "ara-core-tests.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ara::core::test {

namespace {

using swarm::core::kInvalidAddress;
using swarm::core::RouteAction;

struct RouteLog : swarm::core::IRouterObserver {
    std::vector<std::string> lines;
    void onControlSent(std::uint8_t, bool, std::uint32_t) override {}
    void onControlReceived(std::uint8_t, std::uint32_t) override {}
    void onRouteChanged(NodeAddress dest, NodeAddress nextHop, bool added) override {
        lines.push_back(std::to_string(dest) + " " + std::to_string(nextHop) +
                        (added ? " add" : " remove"));
    }
    void onDataDecision(NodeAddress, RouteAction) override {}
    void onStateSize(std::uint32_t entries, std::uint32_t) override {
        lines.push_back("state " + std::to_string(entries));
    }
};

}  // namespace

void TestResetForgetsEverything(Checker& c) {
    Harness h(5);
    RouteLog log;
    h.router.setObserver(&log);
    // Trails toward 1 over 2 and 3, a reverse pointer, a data history entry,
    // and a discovery of this node's own for 9.
    h.router.onAnt(AntMessage{AntType::Forward, 1, 7, 1, 0, 35}, 2);
    h.router.onAnt(AntMessage{AntType::Forward, 1, 7, 2, 1, 35}, 3);
    const DataKey key{1, 7, 17, 4};
    c.check(h.router.admitData(key, 2), "a data packet admitted");
    h.router.onLocalData(9);
    c.check(h.router.discovering(9) && h.router.nextHop(1) == 2, "state before the reset");
    const std::uint32_t lastSequence = h.out.back().decision.message->sequence;

    log.lines.clear();
    h.router.reset();
    c.check(h.router.nextHop(1) == kInvalidAddress && h.router.pheromone(1, 2) == 0.0 &&
                h.router.pheromone(1, 3) == 0.0,
            "no trail survives");
    c.check(!h.router.discovering(9), "no discovery survives");
    c.check(log.lines == std::vector<std::string>{"1 2 remove", "1 3 remove", "state 0"},
            "the observer hears each dropped trail, in order, and the new size");
    c.check(h.router.admitData(key, 2), "the data history is empty: the same key is new");
    h.router.onAnt(AntMessage{AntType::Forward, 1, 7, 1, 0, 35}, 2);
    c.check(h.router.nextHop(1) == 2, "the FANT history is empty: the same FANT lays a trail");

    h.out.clear();
    h.router.onLocalData(8);
    c.check(!h.out.empty() && h.out.back().decision.message &&
                h.out.back().decision.message->sequence == lastSequence + 1,
            "the FANT sequence counter keeps counting");
}

}  // namespace ara::core::test
