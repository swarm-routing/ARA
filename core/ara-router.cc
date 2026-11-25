// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Route discovery ([G02] §3.1), route maintenance by data ([G02] §3.2), loop
// prevention and route failure ([G02] §3.2, §3.3) and their bookkeeping; see
// ara-router.h. Each
// rule names the docs/fidelity.md row it implements.
#include "ara-router.h"

#include "swarm-selection.h"

#include <algorithm>
#include <utility>

namespace ara::core {

using swarm::core::kInvalidAddress;

namespace {

// Fidelity "Ant and data dedup history size": an entry recorded at `at` is
// expired from at + lifetime on, as in swarm-core's DedupHistory (against the
// computed expiry time, so the exact boundary does not depend on rounding).
bool expired(Time at, Time now, Time lifetime) {
    return now >= at + lifetime;
}

}  // namespace

Router::ReversePointers::ReversePointers(std::size_t capacity, Time lifetime)
    : capacity_(std::max<std::size_t>(capacity, 1)), lifetime_(lifetime) {}

void Router::ReversePointers::record(NodeAddress source, std::uint32_t sequence,
                                     NodeAddress previousHop, Time now) {
    const Key key{source, sequence};
    if (const auto it = hops_.find(key); it != hops_.end()) {
        if (!expired(it->second.at, now, lifetime_)) return;
        hops_.erase(it);  // expired: recorded afresh, as the newest
        order_.erase(std::find(order_.begin(), order_.end(), key));
    }
    hops_.emplace(key, TimedHop{previousHop, now});
    order_.push_back(key);
    while (order_.size() > capacity_) {
        hops_.erase(order_.front());
        order_.pop_front();
    }
}

NodeAddress Router::ReversePointers::find(NodeAddress source, std::uint32_t sequence,
                                          Time now) const {
    const auto it = hops_.find({source, sequence});
    if (it == hops_.end() || expired(it->second.at, now, lifetime_)) return kInvalidAddress;
    return it->second.hop;
}

Router::DataHistory::DataHistory(std::size_t capacity, Time lifetime)
    : capacity_(std::max<std::size_t>(capacity, 1)), lifetime_(lifetime) {}

bool Router::DataHistory::record(const DataKey& key, NodeAddress previousHop, Time now) {
    if (const auto it = hops_.find(key); it != hops_.end()) {
        if (!expired(it->second.at, now, lifetime_)) return false;
        hops_.erase(it);  // expired: recorded afresh, as the newest
        order_.erase(std::find(order_.begin(), order_.end(), key));
    }
    hops_.emplace(key, TimedHop{previousHop, now});
    order_.push_back(key);
    while (order_.size() > capacity_) {
        hops_.erase(order_.front());
        order_.pop_front();
    }
    return true;
}

NodeAddress Router::DataHistory::find(const DataKey& key, Time now) const {
    const auto it = hops_.find(key);
    if (it == hops_.end() || expired(it->second.at, now, lifetime_)) return kInvalidAddress;
    return it->second.hop;
}

bool Router::DataHistory::tried(const DataKey& key, Time now) const {
    const auto it = hops_.find(key);
    return it != hops_.end() && !expired(it->second.at, now, lifetime_) && it->second.tried;
}

void Router::DataHistory::markTried(const DataKey& key, NodeAddress previousHop, Time now) {
    record(key, previousHop, now);  // no-op when present and live; bounded as ever
    if (const auto it = hops_.find(key); it != hops_.end()) it->second.tried = true;
}

Router::Router(NodeAddress self, const Config& config, swarm::core::IClock& clock,
               swarm::core::IRng& rng, swarm::core::ITimerScheduler& timers, Sink sink)
    : self_(self), config_(config), clock_(clock), rng_(rng), timers_(timers),
      sink_(std::move(sink)), table_(config.pheromoneCapacity),
      fantHistory_(config.historyCapacity, config.historyLifetime),
      bantHistory_(config.historyCapacity, config.historyLifetime),
      reverse_(config.historyCapacity, config.historyLifetime),
      dataHistory_(config.historyCapacity, config.historyLifetime), lastAged_(clock.now()) {}

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
    // The table does not say which entries expired. Find them first with
    // evaporate()'s own test (value * retain < floor), reading each row in
    // place (no copy), into a reused buffer: steady state allocates nothing
    // but destinations(). The order is (destination, neighbour), so the
    // report is deterministic.
    expiring_.clear();
    for (const NodeAddress dest : table_.destinations()) {
        for (const auto& e : table_.row(dest)) {
            if (e.value * retain < config_.expiryThreshold)
                expiring_.emplace_back(dest, e.neighbor);
        }
    }
    // A retain evaporate() refuses removes nothing: then nothing expired.
    if (table_.evaporate(retain, config_.expiryThreshold) == 0) expiring_.clear();
    lastAged_ = now;
    for (const auto& [dest, over] : expiring_)
        observer_->onRouteChanged(dest, over, false);
    reportState();
}

// The same law, row by row, with lazyDecay (#50): only the row an event
// touches is aged, from its own timestamp, so the cost is that row's, not the
// table's. ϕ·r^a·r^b is not bit-equal to ϕ·r^(a+b), and an entry never read
// keeps its slot below the threshold until its row is touched or reclaim()
// runs (fidelity "Evaporation by eq. 2").
void Router::age(NodeAddress dest) {
    if (!config_.lazyDecay) {
        age();
        return;
    }
    ageRow(dest);
}

void Router::ageRow(NodeAddress dest) {
    const auto row = table_.row(dest);
    if (row.empty()) {
        rowAged_.erase(dest);
        return;
    }
    const Time now = clock_.now();
    const auto [it, fresh] = rowAged_.try_emplace(dest, now);
    if (fresh || !(now > it->second)) return;
    const double retain = swarm::core::evaporationFactor(
        1.0 - config_.evaporationRate, now - it->second, config_.evaporationPeriod);
    it->second = now;
    if (!(retain >= 0.0 && retain <= 1.0)) return;  // as evaporate(): nothing changes
    rowScratch_.assign(row.begin(), row.end());
    bool removed = false;
    for (const auto& e : rowScratch_) {
        // evaporate()'s multiplication and comparison, entry by entry.
        const double value = e.value * retain;
        if (value < config_.expiryThreshold) {
            table_.erase(dest, e.neighbor);
            removed = true;
            if (observer_) observer_->onRouteChanged(dest, e.neighbor, false);
        } else {
            table_.set(dest, e.neighbor, value);
        }
    }
    if (table_.row(dest).empty()) rowAged_.erase(dest);
    if (removed && observer_) reportState();
}

bool Router::reclaim() {
    if (!config_.lazyDecay || !table_.full()) return false;
    const std::size_t before = table_.size();
    for (const NodeAddress dest : table_.destinations())
        ageRow(dest);
    return table_.size() < before;
}

void Router::stamp(NodeAddress dest) {
    if (config_.lazyDecay && !table_.row(dest).empty()) rowAged_.try_emplace(dest, clock_.now());
}

void Router::setObserver(swarm::core::IRouterObserver* observer) {
    observer_ = observer;
    reportedEntries_.reset();
}

// #100: the reset contract of swarm-core's primitives (clear(): as freshly
// constructed, configuration kept), for the whole router. The trails go to
// the observer first, in (destination, neighbour) order, so its counts stay
// right; nextSequence_ keeps counting (see the header).
void Router::reset() {
    if (observer_) {
        expiring_.clear();
        for (const NodeAddress dest : table_.destinations()) {
            for (const auto& e : table_.row(dest))
                expiring_.emplace_back(dest, e.neighbor);
        }
    }
    table_.clear();
    fantHistory_.clear();
    bantHistory_.clear();
    reverse_.clear();
    dataHistory_.clear();
    for (const auto& entry : discoveries_)
        timers_.cancel(entry.second.timer);  // #99: none of them runs after a reset
    discoveries_.clear();
    weights_.clear();
    rowAged_.clear();
    ++epoch_;  // #287: relays scheduled before now are not sent
    lastAged_ = clock_.now();
    if (observer_) {
        for (const auto& [dest, over] : expiring_)
            observer_->onRouteChanged(dest, over, false);
        expiring_.clear();
        reportState();
    }
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
    // Refused when full: no trail (org rule 7); with lazyDecay, once more
    // after reclaiming what expired unread (#50).
    if (!table_.set(dest, over, value) && reclaim()) table_.set(dest, over, value);
    stamp(dest);
    trailWritten(dest, over, existed);
}

void Router::addTrail(NodeAddress dest, NodeAddress over, double delta) {
    const bool existed = table_.contains(dest, over);
    // Refused when full (org rule 7); with lazyDecay, retried as setTrail.
    if (!table_.add(dest, over, delta) && reclaim()) table_.add(dest, over, delta);
    stamp(dest);
    trailWritten(dest, over, existed);
}

AntDecision Router::decided(AntDecision decision, NodeAddress dest) {
    if (observer_) observer_->onDataDecision(dest, decision.action);
    return decision;
}

// Fidelity "Next-hop choice for data among the trails": the highest ϕ;
// bestEntry() gives a tie to the lowest neighbour and kInvalidAddress when no
// ϕ > 0, with no allocation.
NodeAddress Router::bestHop(NodeAddress dest) const {
    return table_.bestEntry(dest).neighbor;
}

// The same row, with statisticalPathSelection on (#135): [GS03] §2.2's
// transition rule, ϕ(d, j) / Σ ϕ(d, k), which §5.2 calls statistical path
// selection. Weights (ϕ / ϕ_max)^1 are proportional to ϕ, in ascending
// neighbour order (deterministic); `prev` is left out unless it is the only
// trail (swarm-core's selectionWeights). One rng_.uniform(), in
// rouletteSelect, and only when two or more trails remain (org rule 5).
NodeAddress Router::dataHop(NodeAddress dest, NodeAddress prev) {
    if (!config_.statisticalPathSelection) return bestHop(dest);
    const auto weights = swarm::core::selectionWeights(table_, dest, 1.0, weights_, prev);
    if (weights.empty()) return kInvalidAddress;
    if (weights.size() == 1) return weights.front().node;
    return swarm::core::rouletteSelect(weights, rng_);
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
    age(toward);
    addTrail(toward, over, config_.deposit);
}

void Router::onDataSent(NodeAddress dest, NodeAddress next) {
    reinforce(dest, next);
}

void Router::onDataReceived(NodeAddress source, NodeAddress prev) {
    reinforce(source, prev);
}

NodeAddress Router::nextHop(NodeAddress dest) {
    age(dest);
    return bestHop(dest);
}

double Router::pheromone(NodeAddress dest, NodeAddress neighbor) {
    age(dest);
    return table_.get(dest, neighbor);
}

AntDecision Router::onTransitData(NodeAddress dest, NodeAddress prev) {
    if (dest == self_) return decided(AntDecision::deliver(), dest);
    age(dest);
    const NodeAddress hop = dataHop(dest, prev);
    if (hop != kInvalidAddress) {
        return decided({swarm::core::RouteAction::Unicast, hop, std::nullopt}, dest);
    }
    return decided(AntDecision::drop(), dest);
}

AntDecision Router::onLocalData(NodeAddress dest) {
    if (dest == self_) return decided(AntDecision::deliver(), dest);
    age(dest);
    const NodeAddress hop = dataHop(dest, kInvalidAddress);  // the source: no previous hop
    if (hop != kInvalidAddress) {
        return decided({swarm::core::RouteAction::Unicast, hop, std::nullopt}, dest);
    }
    if (config_.dataAsFant) return dataFant(dest);
    if (auto it = discoveries_.find(dest); it != discoveries_.end()) {
        it->second.demand = true;  // #5: the source keeps sending
    } else {
        startDiscovery(dest);
    }
    // Fidelity "Data packets waiting for a route": dropped, or held while the
    // discovery runs when bufferDuringDiscovery is on (#5).
    if (holds(dest)) return decided(AntDecision::queue(dest), dest);
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
    age(ant.source);  // both ants touch only the row toward their source
    if (ant.type == AntType::Forward) {
        onForwardAnt(ant, from);
    } else {
        onBackwardAnt(ant, from);
    }
}

void Router::onForwardAnt(const AntMessage& ant, NodeAddress from) {
    // Fidelity "FANT duplicates ... are destroyed": later copies change nothing.
    const Time now = clock_.now();
    if (!fantHistory_.record(ant.source, ant.sequence, now)) return;
    const auto hops = static_cast<std::uint8_t>(std::min<int>(ant.hops + 1, 255));
    layTrail(ant.source, from, hops);
    reverse_.record(ant.source, ant.sequence, from, now);

    if (ant.destination == self_) {
        // Fidelity "Destination destroys the FANT and answers with a BANT",
        // unicast back along the FANT's path ("How the BANT travels back").
        AntMessage bant{AntType::Backward, self_, ant.source, ant.sequence, 0, config_.antTtl};
        // With floodBackwardAnts (#136), "flooded from the target" ([GS03]
        // §4.5): broadcast, unjittered, as a source sends its FANT.
        if (config_.floodBackwardAnts) {
            emit(AntDecision::broadcast(bant));
        } else {
            emit(AntDecision::unicast(from, bant));
        }
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
    scheduleRelay(delay, relay);
}

void Router::onBackwardAnt(const AntMessage& ant, NodeAddress from) {
    // Fidelity "FANT/BANT loop freedom": BANT duplicates dropped like FANTs.
    // Keyed by the discovery, (FANT originator, FANT sequence), as the reverse
    // pointers are: sequences are per originator, so the BANT's own (source,
    // seq) would collide across discoveries toward one destination.
    if (!bantHistory_.record(ant.destination, ant.sequence, clock_.now())) return;
    const auto hops = static_cast<std::uint8_t>(std::min<int>(ant.hops + 1, 255));
    layTrail(ant.source, from, hops);

    if (ant.destination == self_) {
        // Fidelity "Data may be sent once the source receives the BANT".
        endDiscovery(ant.source);
        return;
    }
    if (config_.floodBackwardAnts) {
        // Fidelity "How the BANT travels back", the switch (#136): relayed
        // like a FANT, a jittered broadcast, so nodes off the FANT's reverse
        // path also lay the trail. bantHistory_ above lets each node relay a
        // given BANT once; the TTL bounds it besides. Reverse pointers are
        // still recorded with the FANT, and not read here.
        if (ant.ttl <= 1) return;
        AntMessage relay = ant;
        relay.hops = hops;
        relay.ttl = static_cast<std::uint8_t>(ant.ttl - 1);
        const double draw = rng_.uniform();
        const Time delay = draw * config_.relayJitterMax;
        scheduleRelay(delay, relay);
        return;
    }
    const NodeAddress back = reverse_.find(ant.destination, ant.sequence, clock_.now());
    if (back == kInvalidAddress || ant.ttl <= 1) return;
    AntMessage relay = ant;
    relay.hops = hops;
    relay.ttl = static_cast<std::uint8_t>(ant.ttl - 1);
    emit(AntDecision::unicast(back, relay));
}

// Fidelity "Data packets waiting for a route", the switch (#132): the packet
// is the FANT ([GS03] §4.5, the 2002 version: "ARA used the first packet of a
// connection as the FANT", and packets arriving during discovery made more
// FANTs). A new sequence number per packet; the first one starts the
// discovery and its timeout, which ends it with no retry FANT (onTimeout).
// The FANT goes back to the caller, not through the sink: the adapter sends
// it with the data packet behind it. The source records it, so its echoes
// are dropped, and reports it to the observer as any ant it sends.
AntDecision Router::dataFant(NodeAddress dest) {
    const auto it = discoveries_.find(dest);
    if (it == discoveries_.end() && discoveries_.size() >= config_.maxDiscoveries) {
        return decided(AntDecision::drop(), dest);  // "Concurrent discoveries per node"
    }
    const std::uint32_t sequence = nextSequence_++;
    if (it == discoveries_.end()) {
        discoveries_[dest] = Discovery{sequence, 0, false};
        scheduleTimeout(dest, sequence, 0);
    }
    fantHistory_.record(self_, sequence, clock_.now());
    const AntMessage fant{AntType::Forward, self_, dest, sequence, 0, config_.antTtl};
    if (observer_) {
        observer_->onControlSent(static_cast<std::uint8_t>(fant.type), true,
                                 static_cast<std::uint32_t>(kAntWireSize));
    }
    return decided(AntDecision::broadcast(fant), dest);
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
    fantHistory_.record(self_, sequence, clock_.now());
    emit(AntDecision::broadcast(
        AntMessage{AntType::Forward, self_, dest, sequence, 0, config_.antTtl}));
}

// Fidelity "Discovery retry timeout and count": 2.8 s, doubled per retry.
void Router::scheduleTimeout(NodeAddress dest, std::uint32_t sequence, int retries) {
    Time wait = config_.discoveryTimeout;
    for (int i = 0; i < retries; ++i)
        wait *= 2.0;
    const swarm::core::TimerHandle timer =
        timers_.scheduleCancellable(wait, [this, dest, sequence] { onTimeout(dest, sequence); });
    if (auto it = discoveries_.find(dest); it != discoveries_.end()) it->second.timer = timer;
}

// A relay scheduled before a reset() (a crash, #287) is not sent: it carries
// the reset epoch it was scheduled in, and only that epoch sends it. Works on
// any scheduler, cancelling or not; relays never go stale otherwise.
void Router::scheduleRelay(Time delay, const AntMessage& relay) {
    timers_.schedule(delay, [this, relay, epoch = epoch_] {
        if (epoch == epoch_) emit(AntDecision::broadcast(relay));
    });
}

void Router::endDiscovery(NodeAddress dest) {
    const auto it = discoveries_.find(dest);
    if (it == discoveries_.end()) return;
    timers_.cancel(it->second.timer);
    discoveries_.erase(it);
}

// Fidelity "Discovery retries when no BANT returns": a retry only while the
// source keeps sending (#5), at most maxDiscoveryRetries; otherwise it ends.
void Router::onTimeout(NodeAddress dest, std::uint32_t sequence) {
    const auto it = discoveries_.find(dest);
    if (it == discoveries_.end() || it->second.sequence != sequence) return;  // answered or stale
    age(dest);
    Discovery& d = it->second;
    const bool routed = bestHop(dest) != kInvalidAddress;
    // With dataAsFant (#132) the data itself carries the FANTs: no retry.
    if (routed || !d.demand || d.retries >= config_.maxDiscoveryRetries || config_.dataAsFant) {
        discoveries_.erase(it);
        // Ended with no trail: the data held for it goes too.
        if (!routed && holdsData()) emit(AntDecision::discardPending(dest));
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
    return dataHistory_.record(key, prev, clock_.now());
}

void Router::onDuplicateError(NodeAddress dest, NodeAddress from) {
    // Fidelity "Loop prevention": the sender of the duplicate deactivates
    // that link. The packet itself is a copy of one still on its way: dropped.
    age(dest);
    dropTrail(dest, from);
}

FailureDecision Router::onRouteFailure(const DataKey& key, NodeAddress failed,
                                       NodeAddress cameFrom) {
    const NodeAddress dest = key.dest;
    age(dest);
    dropTrail(dest, failed);  // fidelity "On a failed link": its ϕ to 0
    // Another link, but not back where the packet came from: unflagged, the
    // previous hop would take it for a loop (DUPLICATE_ERROR) and drop it.
    const Time now = clock_.now();
    NodeAddress prev = dataHistory_.find(key, now);
    if (prev == kInvalidAddress && cameFrom != self_) prev = cameFrom;
    // With oneRetryOnFailure (#138, [GS03] §4.3: "only one additional try
    // makes sense"), this node takes one other trail per packet; a second
    // failure of the same packet here goes on as "no other link".
    const bool limited = config_.oneRetryOnFailure;
    if (!(limited && dataHistory_.tried(key, now))) {
        if (const NodeAddress hop = bestHopExcept(dest, prev); hop != kInvalidAddress) {
            if (limited) dataHistory_.markTried(key, prev, now);
            return {decided({swarm::core::RouteAction::Unicast, hop, std::nullopt}, dest), false};
        }
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
    // The highest ϕ > 0 but `except`'s, a tie to the lowest neighbour, read in
    // place (bestEntry, as bestHop).
    return table_.bestEntry(dest, except).neighbor;
}

void Router::dropTrail(NodeAddress dest, NodeAddress over) {
    if (!table_.erase(dest, over)) return;
    if (table_.row(dest).empty()) rowAged_.erase(dest);  // lazyDecay's stamp, if any
    if (observer_) {
        observer_->onRouteChanged(dest, over, false);
        reportState();
    }
}

}  // namespace ara::core
