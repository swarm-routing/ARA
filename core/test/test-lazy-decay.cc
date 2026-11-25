// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Lazy decay (#50, the lazyDecay switch): each event ages only the row it
// touches. On every read it agrees with the eager sweep up to rounding; an
// entry expires at the same threshold when its row is read; an entry never
// read keeps its slot until its row is touched, or until a write refused for
// a full table reclaims every expired entry first.
#include "ara-core-tests.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace ara::core::test {

namespace {

using swarm::core::kInvalidAddress;
using swarm::core::RouteAction;

Config lazy(Config c = {}) {
    c.lazyDecay = true;
    return c;
}

struct TrailLog : swarm::core::IRouterObserver {
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

/// When a trail laid at ϕ = 1 falls below expiryThreshold.
Time expiryTime(const Config& c) {
    return c.evaporationPeriod * std::log(c.expiryThreshold) / std::log(1.0 - c.evaporationRate);
}

}  // namespace

void TestLazyDecayAgreesWithEager(Checker& c) {
    Harness eager(9);
    Harness lazily(9, lazy());
    // Trails toward 1..4 over neighbours 5..7, raised by data at uneven
    // times, rows read in an order unrelated to when they were written.
    std::uint32_t seq = 1;
    double worst = 0.0;
    bool sameHops = true;
    for (int step = 0; step < 60; ++step) {
        const auto src = static_cast<NodeAddress>(1 + step % 4);
        const auto over = static_cast<NodeAddress>(5 + (step * 7) % 3);
        const auto hops = static_cast<std::uint8_t>(1 + step % 5);
        const Time dt = 0.05 + 0.37 * static_cast<double>((step * 13) % 7);
        for (Harness* h : {&eager, &lazily}) {
            h->events.advance(dt);
            h->router.onAnt(AntMessage{AntType::Forward, src, 99, seq, hops, 35}, over);
            if (step % 3 == 0)
                h->router.onDataSent(static_cast<NodeAddress>(1 + (step / 3) % 4), over);
        }
        ++seq;
        const auto read = static_cast<NodeAddress>(1 + (step * 3) % 4);
        for (NodeAddress n = 5; n <= 7; ++n) {
            const double a = eager.router.pheromone(read, n);
            const double b = lazily.router.pheromone(read, n);
            if (a > 0.0 || b > 0.0) worst = std::max(worst, std::abs(a - b) / std::max(a, b));
        }
        sameHops = sameHops && eager.router.nextHop(read) == lazily.router.nextHop(read);
    }
    c.check(worst < 1e-12, "every read within 1e-12 (relative) of the eager sweep");
    c.check(sameHops, "the same next hop on every read");
}

void TestLazyDecayExpiresAtTheThreshold(Checker& c) {
    const Time t = expiryTime(Config{});
    for (const bool on : {false, true}) {
        Config cfg;
        cfg.lazyDecay = on;
        Harness before(9, cfg);
        Harness after(9, cfg);
        for (Harness* h : {&before, &after})
            h->router.onAnt(AntMessage{AntType::Forward, 1, 99, 1, 0, 35}, 5);  // ϕ = 1/(0 + 1)
        before.events.advance(t - 0.05);
        after.events.advance(t + 0.05);
        c.check(before.router.nextHop(1) == 5 && before.router.pheromone(1, 5) > 0.0,
                on ? "lazy: kept just above the threshold"
                   : "eager: kept just above the threshold");
        c.check(after.router.nextHop(1) == kInvalidAddress && after.router.pheromone(1, 5) == 0.0,
                on ? "lazy: gone just below it" : "eager: gone just below it");
    }
}

void TestLazyDecayReclaimsUnreadEntries(Checker& c) {
    Config cfg = lazy();
    cfg.pheromoneCapacity = 2;
    Harness h(9, cfg);
    TrailLog log;
    h.router.setObserver(&log);
    h.router.onAnt(AntMessage{AntType::Forward, 1, 99, 1, 1, 35}, 5);
    h.router.onAnt(AntMessage{AntType::Forward, 3, 99, 2, 1, 35}, 5);
    h.events.advance(expiryTime(cfg) + 1.0);

    // Unread, both expired entries keep their slots: touching another row
    // drops nothing (the eager sweep would drop both here).
    log.lines.clear();
    c.check(h.router.nextHop(7) == kInvalidAddress && log.lines.empty(),
            "an unrelated read drops no unread entry");

    // A trail toward 4 is refused by the full table, reclaims both, then
    // lands.
    h.router.onAnt(AntMessage{AntType::Forward, 4, 99, 3, 1, 35}, 6);
    c.check(h.router.nextHop(4) == 6, "the refused write lands after the reclaim");
    c.check(log.lines == std::vector<std::string>{"1 5 remove", "state 1", "3 5 remove", "state 0",
                                                  "4 6 add", "state 1"},
            "the observer hears each reclaimed entry, then the new trail");

    // Nothing expired: no reclaim, the write is refused (org rule 7).
    h.router.onAnt(AntMessage{AntType::Forward, 1, 99, 4, 1, 35}, 6);
    h.router.onAnt(AntMessage{AntType::Forward, 2, 99, 5, 1, 35}, 6);
    c.check(h.router.nextHop(1) == 6 && h.router.nextHop(2) == kInvalidAddress,
            "a full table of live trails still refuses");
}

}  // namespace ara::core::test
