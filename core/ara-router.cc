// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Route discovery ([G02] §3.1) and its bookkeeping; see ara-router.h. Each
// rule names the docs/fidelity.md row it implements.
#include "ara-router.h"

#include "swarm-selection.h"

#include <algorithm>
#include <utility>

namespace ara::core {

using swarm::core::kInvalidAddress;

Router::ReversePointers::ReversePointers(std::size_t capacity)
    : capacity_(std::max<std::size_t>(capacity, 1)) {}

void Router::ReversePointers::record(NodeAddress source, std::uint32_t sequence,
                                     NodeAddress previousHop) {
    const Key key{source, sequence};
    if (!hops_.emplace(key, previousHop).second) return;
    order_.push_back(key);
    while (order_.size() > capacity_) {
        hops_.erase(order_.front());
        order_.pop_front();
    }
}

NodeAddress Router::ReversePointers::find(NodeAddress source, std::uint32_t sequence) const {
    const auto it = hops_.find({source, sequence});
    return it != hops_.end() ? it->second : kInvalidAddress;
}

Router::Router(NodeAddress self, const Config& config, swarm::core::IClock& clock,
               swarm::core::IRng& rng, swarm::core::ITimerScheduler& timers, Sink sink)
    : self_(self), config_(config), clock_(clock), rng_(rng), timers_(timers),
      sink_(std::move(sink)), table_(config.pheromoneCapacity),
      fantHistory_(config.historyCapacity), bantHistory_(config.historyCapacity),
      reverse_(config.historyCapacity), lastAged_(clock.now()) {}

// Fidelity "Evaporation by eq. 2": ϕ := (1 − q)·ϕ per period, evaluated
// continuously as ϕ·(1 − q)^(Δt / period) whenever the table is read or
// written; "A trail decays to zero": entries below the expiry threshold go.
void Router::age() {
    const Time now = clock_.now();
    if (!(now > lastAged_)) return;
    const double retain = swarm::core::evaporationFactor(
        1.0 - config_.evaporationRate, now - lastAged_, config_.evaporationPeriod);
    table_.evaporate(retain, config_.expiryThreshold);
    lastAged_ = now;
}

// Fidelity "Next-hop choice for data among the trails": the highest ϕ;
// entries() is in ascending neighbour order, so a tie goes to the lowest.
NodeAddress Router::bestHop(NodeAddress dest) const {
    NodeAddress best = kInvalidAddress;
    double bestValue = 0.0;
    for (const auto& e : table_.entries(dest)) {
        if (e.value > bestValue) {
            best = e.neighbor;
            bestValue = e.value;
        }
    }
    return best;
}

// Fidelity "A node seeing a FANT for the first time records ...": ϕ raised to
// at least 1/hops (#4); the BANT does the same toward the destination.
void Router::layTrail(NodeAddress toward, NodeAddress over, std::uint8_t hops) {
    const double initial = 1.0 / static_cast<double>(std::max<std::uint8_t>(hops, 1));
    if (table_.get(toward, over) < initial) {
        table_.set(toward, over, initial);  // refused when full: no trail (org rule 7)
    }
}

NodeAddress Router::nextHop(NodeAddress dest) {
    age();
    return bestHop(dest);
}

double Router::pheromone(NodeAddress dest, NodeAddress neighbor) {
    age();
    return table_.get(dest, neighbor);
}

AntDecision Router::onLocalData(NodeAddress dest) {
    if (dest == self_) return AntDecision::deliver();
    age();
    const NodeAddress hop = bestHop(dest);
    if (hop != kInvalidAddress) {
        return {swarm::core::RouteAction::Unicast, hop, std::nullopt};
    }
    // Fidelity "Data packets waiting for a route": no buffering, dropped.
    if (auto it = discoveries_.find(dest); it != discoveries_.end()) {
        it->second.demand = true;  // #5: the source keeps sending
    } else {
        startDiscovery(dest);
    }
    return AntDecision::drop();
}

void Router::onAnt(const AntMessage& ant, NodeAddress from) {
    if (ant.source == self_ || from == kInvalidAddress) return;
    age();
    if (ant.type == AntType::Forward) {
        onForwardAnt(ant, from);
    } else {
        onBackwardAnt(ant, from);
    }
}

void Router::onForwardAnt(const AntMessage& ant, NodeAddress from) {
    // Fidelity "FANT duplicates ... are destroyed": later copies change nothing.
    if (!fantHistory_.record(ant.source, ant.sequence)) return;
    const auto hops = static_cast<std::uint8_t>(std::min<int>(ant.hops + 1, 255));
    layTrail(ant.source, from, hops);
    reverse_.record(ant.source, ant.sequence, from);

    if (ant.destination == self_) {
        // Fidelity "Destination destroys the FANT and answers with a BANT",
        // unicast back along the FANT's path ("How the BANT travels back").
        AntMessage bant{AntType::Backward, self_, ant.source, ant.sequence, 0, config_.antTtl};
        sink_(AntDecision::unicast(from, bant));
        return;
    }
    if (ant.ttl <= 1) return;  // fidelity "FANT maximum hops / TTL"

    AntMessage relay = ant;
    relay.hops = hops;
    relay.ttl = static_cast<std::uint8_t>(ant.ttl - 1);
    // Fidelity "FANT relay jitter": U[0, max) drawn into a named local from
    // the injected RNG (org rule 5), then a broadcast after that delay.
    const double draw = rng_.uniform();
    const Time delay = draw * config_.relayJitterMax;
    timers_.schedule(delay, [this, relay] { sink_(AntDecision::broadcast(relay)); });
}

void Router::onBackwardAnt(const AntMessage& ant, NodeAddress from) {
    // Fidelity "FANT/BANT loop freedom": BANT duplicates dropped like FANTs.
    // Keyed by the discovery, (FANT originator, FANT sequence), as the reverse
    // pointers are: sequences are per originator, so the BANT's own (source,
    // seq) would collide across discoveries toward one destination.
    if (!bantHistory_.record(ant.destination, ant.sequence)) return;
    const auto hops = static_cast<std::uint8_t>(std::min<int>(ant.hops + 1, 255));
    layTrail(ant.source, from, hops);

    if (ant.destination == self_) {
        // Fidelity "Data may be sent once the source receives the BANT".
        discoveries_.erase(ant.source);
        return;
    }
    const NodeAddress back = reverse_.find(ant.destination, ant.sequence);
    if (back == kInvalidAddress || ant.ttl <= 1) return;
    AntMessage relay = ant;
    relay.hops = hops;
    relay.ttl = static_cast<std::uint8_t>(ant.ttl - 1);
    sink_(AntDecision::unicast(back, relay));
}

void Router::startDiscovery(NodeAddress dest) {
    // Fidelity "Concurrent discoveries per node": none beyond the cap.
    if (discoveries_.size() >= config_.maxDiscoveries) return;
    const std::uint32_t sequence = nextSequence_++;
    discoveries_[dest] = Discovery{sequence, 0, false};
    sendFant(dest, sequence);
    scheduleTimeout(dest, sequence, 0);
}

// Fidelity "Forward ant (FANT): broadcast by the sender ... unique sequence
// number". The source remembers its own FANT so that echoes are dropped.
void Router::sendFant(NodeAddress dest, std::uint32_t sequence) {
    fantHistory_.record(self_, sequence);
    sink_(AntDecision::broadcast(
        AntMessage{AntType::Forward, self_, dest, sequence, 0, config_.antTtl}));
}

// Fidelity "Discovery retry timeout and count": 2.8 s, doubled per retry.
void Router::scheduleTimeout(NodeAddress dest, std::uint32_t sequence, int retries) {
    Time wait = config_.discoveryTimeout;
    for (int i = 0; i < retries; ++i)
        wait *= 2.0;
    timers_.schedule(wait, [this, dest, sequence] { onTimeout(dest, sequence); });
}

// Fidelity "Discovery retries when no BANT returns": a retry only while the
// source keeps sending (#5), at most maxDiscoveryRetries; otherwise it ends.
void Router::onTimeout(NodeAddress dest, std::uint32_t sequence) {
    const auto it = discoveries_.find(dest);
    if (it == discoveries_.end() || it->second.sequence != sequence) return;  // answered or stale
    age();
    Discovery& d = it->second;
    if (bestHop(dest) != kInvalidAddress || !d.demand || d.retries >= config_.maxDiscoveryRetries) {
        discoveries_.erase(it);
        return;
    }
    const int retries = ++d.retries;
    const std::uint32_t next = nextSequence_++;
    d.demand = false;
    d.sequence = next;
    // Locals, not `d`: the sink may reach back into this router.
    sendFant(dest, next);
    scheduleTimeout(dest, next, retries);
}

}  // namespace ara::core
