// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Dependency-free test support for ARA's core tests: a failure collector, a
// seeded RNG, a discrete-event clock and scheduler behind swarm-core's ports,
// and a tiny static network that carries ants between routers through the
// codec. The runners (run-core-tests.cc here; the ns-3 suite later) report
// what the Checker collects.
#ifndef ARA_CORE_TEST_SUPPORT_H
#define ARA_CORE_TEST_SUPPORT_H

#include "../ara-ant.h"
#include "../ara-router.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace ara::core::test {

/// Collects failed checks, each with its file and line.
class Checker {
public:
    void check(bool ok, std::string_view what,
               std::source_location where = std::source_location::current()) {
        if (ok) return;
        failures_.push_back(std::string(where.file_name()) + ":" + std::to_string(where.line()) +
                            ": " + std::string(what));
    }
    const std::vector<std::string>& failures() const { return failures_; }

private:
    std::vector<std::string> failures_;
};

/// SplitMix64: fully specified, so a seed gives the same sequence everywhere.
class SeededRng : public swarm::core::IRng {
public:
    explicit SeededRng(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform() override {
        ++draws_;
        return static_cast<double>(next() >> 11) * 0x1.0p-53;
    }
    int uniformInt(int n) override {
        if (n <= 0) return 0;
        ++draws_;
        return static_cast<int>(next() % static_cast<std::uint64_t>(n));
    }
    int draws() const { return draws_; }

private:
    std::uint64_t state_;
    int draws_ = 0;
};

/// The clock and timer ports as one discrete-event queue: events run in
/// (time, scheduling order), so a run is fully deterministic.
class EventQueue : public swarm::core::IClock, public swarm::core::ITimerScheduler {
public:
    Time now() const override { return now_; }
    void schedule(Time delay, std::function<void()> callback) override {
        events_.push(Event{now_ + (delay > 0.0 ? delay : 0.0), next_++, std::move(callback)});
    }
    /// Runs every event due at or before `until`, then sets the clock to it.
    void runUntil(Time until) {
        while (!events_.empty() && events_.top().at <= until) {
            Event e = events_.top();
            events_.pop();
            now_ = e.at;
            e.callback();
        }
        if (until > now_) now_ = until;
    }
    void advance(Time dt) { runUntil(now_ + dt); }
    std::size_t pending() const { return events_.size(); }

private:
    struct Event {
        Time at;
        std::uint64_t order;
        std::function<void()> callback;
        bool operator>(const Event& o) const {
            return std::tie(at, order) > std::tie(o.at, o.order);
        }
    };
    Time now_ = 0.0;
    std::uint64_t next_ = 0;
    std::priority_queue<Event, std::vector<Event>, std::greater<>> events_;
};

/// One router's emissions, recorded as they leave it.
struct Sent {
    Time at;
    AntDecision decision;
};

/// A static network of routers on bidirectional links. Every ant crosses the
/// codec (encode, then decode at the receiver) and a link delay; the trace
/// lines are what the determinism test compares.
class Network {
public:
    explicit Network(std::uint64_t seed, Config config = {}, Time linkDelay = 0.001)
        : rng_(seed), config_(config), linkDelay_(linkDelay) {}

    void addLink(NodeAddress a, NodeAddress b) {
        node(a);
        node(b);
        links_[a].insert(b);
        links_[b].insert(a);
    }
    Router& node(NodeAddress a) {
        auto it = routers_.find(a);
        if (it == routers_.end()) {
            auto r = std::make_unique<Router>(a, config_, events_, rng_, events_,
                                              [this, a](const AntDecision& d) { emit(a, d); });
            it = routers_.emplace(a, std::move(r)).first;
        }
        return *it->second;
    }
    EventQueue& events() { return events_; }
    SeededRng& rng() { return rng_; }
    const std::vector<std::string>& trace() const { return trace_; }
    int sent(AntType type) const { return type == AntType::Forward ? fants_ : bants_; }

private:
    void emit(NodeAddress from, const AntDecision& d) {
        if (!d.message) return;
        std::vector<std::uint8_t> bytes;
        encodeAnt(*d.message, bytes);
        const AntMessage& m = *d.message;
        (m.type == AntType::Forward ? fants_ : bants_)++;
        char line[160];
        std::snprintf(line, sizeof line, "%.9f %d %s %s src=%d dst=%d seq=%u hops=%u ttl=%u to=%d",
                      events_.now(), from,
                      d.action == swarm::core::RouteAction::Broadcast ? "bcast" : "ucast",
                      m.type == AntType::Forward ? "FANT" : "BANT", m.source, m.destination,
                      m.sequence, m.hops, m.ttl, d.nextHop);
        trace_.emplace_back(line);
        for (NodeAddress to : links_[from]) {
            if (d.action == swarm::core::RouteAction::Unicast && to != d.nextHop) continue;
            events_.schedule(linkDelay_, [this, bytes, from, to] {
                if (auto ant = decodeAnt(bytes)) node(to).onAnt(*ant, from);
            });
        }
    }

    EventQueue events_;
    SeededRng rng_;
    Config config_;
    Time linkDelay_;
    std::map<NodeAddress, std::unique_ptr<Router>> routers_;
    std::map<NodeAddress, std::set<NodeAddress>> links_;
    std::vector<std::string> trace_;
    int fants_ = 0;
    int bants_ = 0;
};

/// A single router whose emissions are only recorded, for unit tests.
struct Harness {
    explicit Harness(NodeAddress self, Config config = {}, std::uint64_t seed = 1)
        : rng(seed), router(self, config, events, rng, events,
                            [this](const AntDecision& d) { out.push_back({events.now(), d}); }) {}
    EventQueue events;
    SeededRng rng;
    std::vector<Sent> out;
    Router router;
};

}  // namespace ara::core::test

#endif  // ARA_CORE_TEST_SUPPORT_H
