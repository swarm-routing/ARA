// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// ARA's per-node state and decision logic, written fresh from Güneş, Sorges &
// Bouazizi 2002 ([G02]) and docs/fidelity.md (golden rule 12). This first
// slice is route discovery ([G02] §3.1): the FANT flood, the BANT answer, the
// pheromone trails they lay, evaporation and the deterministic next-hop
// choice, and the discovery retries of the 2026-11-10 decisions (#4, #5).
// Data reinforcement, duplicate errors and route-failure backtracking
// ([G02] §3.2, §3.3) are the next slices.
//
// The router never touches a simulator (golden rule 10). Time, randomness and
// deferred work come through swarm-core's ports; every ant it sends leaves
// through the Sink as a swarm-core RouteDecision, which the adapter carries
// out. A fixed seed of the injected IRng gives byte-identical behaviour (org
// rule 5): the only draw is the relay jitter, one uniform() per relayed FANT.
#ifndef ARA_CORE_ROUTER_H
#define ARA_CORE_ROUTER_H

#include "ara-ant.h"
#include "ara-config.h"
#include "swarm-dedup-history.h"
#include "swarm-pheromone-table.h"
#include "swarm-ports.h"
#include "swarm-route-decision.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <utility>

namespace ara::core {

using AntDecision = swarm::core::RouteDecision<AntMessage>;

class Router {
public:
    /// Receives every ant this node sends: a Broadcast (FANT) or a Unicast to
    /// a neighbour (BANT), each with its message set.
    using Sink = std::function<void(const AntDecision&)>;

    /// The ports must outlive the router, and the timers it schedules must
    /// not fire after it is destroyed (the adapter cancels them with it).
    Router(NodeAddress self, const Config& config, swarm::core::IClock& clock,
           swarm::core::IRng& rng, swarm::core::ITimerScheduler& timers, Sink sink);

    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;

    /// This node has a data packet for `dest`. Unicast to the next hop when a
    /// trail exists. Otherwise Drop (no buffering, fidelity "Data packets
    /// waiting for a route"), and either a discovery starts or, while one
    /// runs, the packet counts as demand for its next retry (#5).
    AntDecision onLocalData(NodeAddress dest);

    /// An ant arrived from the one-hop neighbour `from`.
    void onAnt(const AntMessage& ant, NodeAddress from);

    /// The neighbour with the highest pheromone toward `dest` (ties: the
    /// lowest address), or kInvalidAddress when there is no trail. Fidelity
    /// "Next-hop choice for data among the trails": deterministic (#15).
    NodeAddress nextHop(NodeAddress dest);

    /// The pheromone toward `dest` over `neighbor`, evaporated to now.
    double pheromone(NodeAddress dest, NodeAddress neighbor);

    /// True while a discovery for `dest` is waiting for its BANT.
    bool discovering(NodeAddress dest) const { return discoveries_.contains(dest); }

    NodeAddress self() const { return self_; }

private:
    struct Discovery {
        std::uint32_t sequence = 0;  ///< of the latest FANT
        int retries = 0;
        bool demand = false;  ///< data for the destination arrived since that FANT
    };

    /// (FANT source, sequence) -> the neighbour the FANT came from: the path
    /// the BANT is unicast back along (fidelity "How the BANT travels back").
    /// Recorded with the FANT history and bounded by the same cap, oldest
    /// evicted first (org rule 7).
    class ReversePointers {
    public:
        explicit ReversePointers(std::size_t capacity);
        void record(NodeAddress source, std::uint32_t sequence, NodeAddress previousHop);
        NodeAddress find(NodeAddress source, std::uint32_t sequence) const;
        std::size_t size() const { return hops_.size(); }

    private:
        using Key = std::pair<NodeAddress, std::uint32_t>;
        std::size_t capacity_;
        std::map<Key, NodeAddress> hops_;
        std::deque<Key> order_;
    };

    void age();
    NodeAddress bestHop(NodeAddress dest) const;
    void layTrail(NodeAddress toward, NodeAddress over, std::uint8_t hops);
    void onForwardAnt(const AntMessage& ant, NodeAddress from);
    void onBackwardAnt(const AntMessage& ant, NodeAddress from);
    void startDiscovery(NodeAddress dest);
    void sendFant(NodeAddress dest, std::uint32_t sequence);
    void scheduleTimeout(NodeAddress dest, std::uint32_t sequence, int retries);
    void onTimeout(NodeAddress dest, std::uint32_t sequence);

    NodeAddress self_;
    Config config_;
    swarm::core::IClock& clock_;
    swarm::core::IRng& rng_;
    swarm::core::ITimerScheduler& timers_;
    Sink sink_;

    swarm::core::PheromoneTable table_;
    swarm::core::DedupHistory fantHistory_;
    swarm::core::DedupHistory bantHistory_;
    ReversePointers reverse_;
    std::map<NodeAddress, Discovery> discoveries_;
    std::uint32_t nextSequence_ = 1;
    Time lastAged_;
};

}  // namespace ara::core

#endif  // ARA_CORE_ROUTER_H
