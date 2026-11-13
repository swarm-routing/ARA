// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Route discovery ([G02] §3.1), route maintenance by data ([G02] §3.2) and
// their bookkeeping; see ara-router.h. Each
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

Router::DataHistory::DataHistory(std::size_t capacity)
    : capacity_(std::max<std::size_t>(capacity, 1)) {}

bool Router::DataHistory::record(const DataKey& key, NodeAddress previousHop) {
    if (!hops_.emplace(key, previousHop).second) return false;
    order_.push_back(key);
    while (order_.size() > capacity_) {
        hops_.erase(order_.front());
        order_.pop_front();
    }
    return true;
}

NodeAddress Router::DataHistory::find(const DataKey& key) const {
    const auto it = hops_.find(key);
    return it != hops_.end() ? it->second : kInvalidAddress;
}

Router::Router(NodeAddress self, const Config& config, swarm::core::IClock& clock,
               swarm::core::IRng& rng, swarm::core::ITimerScheduler& timers, Sink sink)
    : self_(self), config_(config), clock_(clock), rng_(rng), timers_(timers),
      sink_(std::move(sink)), table_(config.pheromoneCapacity),
      fantHistory_(config.historyCapacity), bantHistory_(config.historyCapacity),
      reverse_(config.historyCapacity), dataHistory_(config.historyCapacity),
      lastAged_(clock.now()) {}

// Fidelity "Evaporation by eq. 2": ϕ := (1 − q)·ϕ per period, evaluated
// continuously as ϕ·(1 − q)^(Δt / period) whenever the table is read or
// written; "A trail decays to zero": entries below the expiry threshold go.
void Router::age() {
    const Time now = clock_.now();
    if (!(now > lastAged_)) return;
    const double retain = swarm::core::evaporationFactor(
        1.0 - config_.evaporationRate, now - lastAged_, config_.evaporationPeriod);
    if (!observer_) {
        table_.evaporate(retain, config_.expiryThreshold);
        lastAged_ = now;
        return;
    }
    // The table does not say which entries expired: compare before and after,
    // in (destination, neighbour) order, so the report is deterministic.
    std::vector<std::pair<NodeAddress, NodeAddress>> before;
    for (const NodeAddress dest : table_.destinations()) {
        for (const auto& e : table_.entries(dest))
            before.emplace_back(dest, e.neighbor);
    }
    table_.evaporate(retain, config_.expiryThreshold);
    lastAged_ = now;
    for (const auto& [dest, over] : before) {
        if (!table_.contains(dest, over)) observer_->onRouteChanged(dest, over, false);
    }
    reportState();
}

void Router::setObserver(swarm::core::IRouterObserver* observer) {
    observer_ = observer;
    reportedEntries_.reset();
}

void Router::emit(const AntDecision& decision) {
    if (observer_ && decision.message) {
        observer_->onControlSent(static_cast<std::uint8_t>(decision.message->type),
                                 decision.action == swarm::core::RouteAction::Broadcast,
                                 static_cast<std::uint32_t>(kAntWireSize));
    }
    sink_(decision);
}

// An entry that was not in the table and is now is a route added; a refused
// write (full table, org rule 7) or a raise of an existing entry is not.
void Router::trailWritten(NodeAddress dest, NodeAddress over, bool existed) {
    if (!observer_) return;
    if (!existed && table_.contains(dest, over)) observer_->onRouteChanged(dest, over, true);
    reportState();
}

void Router::reportState() {
    if (reportedEntries_ && *reportedEntries_ == table_.size()) return;
    reportedEntries_ = table_.size();
    observer_->onStateSize(static_cast<std::uint32_t>(table_.size()),
                           static_cast<std::uint32_t>(table_.size()) * kTrailEntryBytes);
}

void Router::setTrail(NodeAddress dest, NodeAddress over, double value) {
    const bool existed = table_.contains(dest, over);
    table_.set(dest, over, value);  // refused when full: no trail (org rule 7)
    trailWritten(dest, over, existed);
}

void Router::addTrail(NodeAddress dest, NodeAddress over, double delta) {
    const bool existed = table_.contains(dest, over);
    table_.add(dest, over, delta);  // refused when full (org rule 7)
    trailWritten(dest, over, existed);
}

AntDecision Router::decided(AntDecision decision, NodeAddress dest) {
    if (observer_) observer_->onDataDecision(dest, decision.action);
    return decision;
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
        setTrail(toward, over, initial);
    }
}

// Fidelity "Route maintenance by data": [G02] §3.2's ϕ := ϕ + Δϕ (eq. 1) on
// the entry the data packet used, after evaporating to now. Nothing toward
// this node itself, and nothing over an unknown neighbour.
void Router::reinforce(NodeAddress toward, NodeAddress over) {
    if (toward == self_ || toward == kInvalidAddress || over == kInvalidAddress) return;
    age();
    addTrail(toward, over, config_.deposit);
}

void Router::onDataSent(NodeAddress dest, NodeAddress next) {
    reinforce(dest, next);
}

void Router::onDataReceived(NodeAddress source, NodeAddress prev) {
    reinforce(source, prev);
}

NodeAddress Router::nextHop(NodeAddress dest) {
    age();
    return bestHop(dest);
}

double Router::pheromone(NodeAddress dest, NodeAddress neighbor) {
    age();
    return table_.get(dest, neighbor);
}

AntDecision Router::onTransitData(NodeAddress dest) {
    if (dest == self_) return decided(AntDecision::deliver(), dest);
    age();
    const NodeAddress hop = bestHop(dest);
    if (hop != kInvalidAddress) {
        return decided({swarm::core::RouteAction::Unicast, hop, std::nullopt}, dest);
    }
    return decided(AntDecision::drop(), dest);
}

AntDecision Router::onLocalData(NodeAddress dest) {
    if (dest == self_) return decided(AntDecision::deliver(), dest);
    age();
    const NodeAddress hop = bestHop(dest);
    if (hop != kInvalidAddress) {
        return decided({swarm::core::RouteAction::Unicast, hop, std::nullopt}, dest);
    }
    // Fidelity "Data packets waiting for a route": no buffering, dropped.
    if (auto it = discoveries_.find(dest); it != discoveries_.end()) {
        it->second.demand = true;  // #5: the source keeps sending
    } else {
        startDiscovery(dest);
    }
    return decided(AntDecision::drop(), dest);
}

void Router::onAnt(const AntMessage& ant, NodeAddress from) {
    if (from == kInvalidAddress) return;
    // Received and processed, even if only to drop an echo of our own FANT.
    if (observer_) {
        observer_->onControlReceived(static_cast<std::uint8_t>(ant.type),
                                     static_cast<std::uint32_t>(kAntWireSize));
    }
    if (ant.source == self_) return;
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
        emit(AntDecision::unicast(from, bant));
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
    timers_.schedule(delay, [this, relay] { emit(AntDecision::broadcast(relay)); });
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
    emit(AntDecision::unicast(back, relay));
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
    emit(AntDecision::broadcast(
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

// --- loop prevention and route failure ([G02] §3.2, §3.3) ----------------------

bool Router::admitData(const DataKey& key, NodeAddress prev) {
    return dataHistory_.record(key, prev);
}

void Router::onDuplicateError(NodeAddress dest, NodeAddress from) {
    // Fidelity "Loop prevention": the sender of the duplicate deactivates
    // that link. The packet itself is a copy of one still on its way: dropped.
    age();
    dropTrail(dest, from);
}

FailureDecision Router::onRouteFailure(const DataKey& key, NodeAddress failed) {
    const NodeAddress dest = key.dest;
    age();
    dropTrail(dest, failed);  // fidelity "On a failed link": its ϕ to 0
    // Another link, but not back where the packet came from: unflagged, the
    // previous hop would take it for a loop (DUPLICATE_ERROR) and drop it.
    const NodeAddress prev = dataHistory_.find(key);
    if (const NodeAddress hop = bestHopExcept(dest, prev); hop != kInvalidAddress) {
        return {decided({swarm::core::RouteAction::Unicast, hop, std::nullopt}, dest), false};
    }
    // Fidelity "No other link": back to the previous hop (#15), up to the source.
    if (prev != kInvalidAddress) {
        return {decided({swarm::core::RouteAction::Unicast, prev, std::nullopt}, dest), true};
    }
    if (key.source == self_) {
        if (auto it = discoveries_.find(dest); it != discoveries_.end()) {
            it->second.demand = true;
        } else {
            startDiscovery(dest);
        }
    }
    return {decided(AntDecision::drop(), dest), false};
}

NodeAddress Router::bestHopExcept(NodeAddress dest, NodeAddress except) const {
    NodeAddress best = kInvalidAddress;
    double bestValue = 0.0;
    for (const auto& e : table_.entries(dest)) {
        if (e.neighbor != except && e.value > bestValue) {
            best = e.neighbor;
            bestValue = e.value;
        }
    }
    return best;
}

void Router::dropTrail(NodeAddress dest, NodeAddress over) {
    if (!table_.erase(dest, over)) return;
    if (observer_) {
        observer_->onRouteChanged(dest, over, false);
        reportState();
    }
}

}  // namespace ara::core
