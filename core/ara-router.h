// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// ARA's per-node state and decision logic, written fresh from Güneş, Sorges &
// Bouazizi 2002 ([G02]) and docs/fidelity.md (golden rule 12). This first
// slice is route discovery ([G02] §3.1): the FANT flood, the BANT answer, the
// pheromone trails they lay, evaporation and the deterministic next-hop
// choice, and the discovery retries of the 2026-11-10 decisions (#4, #5).
// The second is route maintenance by data ([G02] §3.2): every data hop adds
// Δϕ toward both ends. Duplicate errors and route-failure backtracking
// ([G02] §3.2, §3.3) are the next slices.
//
// The router never touches a simulator (golden rule 10). Time, randomness and
// deferred work come through swarm-core's ports; every ant it sends leaves
// through the Sink as a swarm-core RouteDecision, which the adapter carries
// out. An optional swarm-core IRouterObserver (setObserver) is told what the
// router sends and receives, which routes it adds and drops, what it decides
// for data and how big its trail table is; it only reads, so a run is the same
// with or without one (org rule 5; test-observer.cc). A slice that adds a
// control message or a table write reports it through the same helpers. A fixed seed of the injected IRng gives byte-identical behaviour (org
// rule 5): the only draw is the relay jitter, one uniform() per relayed FANT.
#ifndef ARA_CORE_ROUTER_H
#define ARA_CORE_ROUTER_H

#include "ara-ant.h"
#include "ara-config.h"
#include "swarm-dedup-history.h"
#include "swarm-observer.h"
#include "swarm-pheromone-table.h"
#include "swarm-ports.h"
#include "swarm-route-decision.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

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

    /// A data packet for `dest` that arrived from a neighbour and is not this
    /// node's own: Deliver when `dest` is this node, else Unicast to the next
    /// trail or Drop when there is none (no buffering). Like nextHop(), plus
    /// the report to the observer.
    AntDecision onTransitData(NodeAddress dest);

    /// Route maintenance by data ([G02] §3.2, fidelity "Route maintenance by
    /// data"): this node, the source or a relay, hands a data packet for
    /// `dest` to its neighbour `next`, and adds Δϕ to (dest, next).
    void onDataSent(NodeAddress dest, NodeAddress next);

    /// The other half: a data packet from `source` arrived from the neighbour
    /// `prev` (at a relay or at the destination), which adds Δϕ to
    /// (source, prev). A deposit on a missing entry creates it at Δϕ (a
    /// reading accepted by the maintainer on 2026-11-11,
    /// https://github.com/swarm-routing/ARA/issues/5#issuecomment-6783812825);
    /// the table's cap refuses it when full.
    void onDataReceived(NodeAddress source, NodeAddress prev);

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

    /// Reports to `observer` from now on (null: none, the default; one pointer
    /// check per event). The observer must outlive the router. The trail
    /// table's size is reported again at its next change.
    void setObserver(swarm::core::IRouterObserver* observer);

    /// What one trail table entry takes: the destination and the neighbour
    /// address and the pheromone value (the payload, not the map's nodes).
    static constexpr std::uint32_t kTrailEntryBytes =
        static_cast<std::uint32_t>(2 * sizeof(NodeAddress) + sizeof(double));

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
    /// The sink, after telling the observer (an ant is kAntWireSize bytes).
    void emit(const AntDecision& decision);
    /// Table writes that tell the observer of a new entry and a new size.
    void setTrail(NodeAddress dest, NodeAddress over, double value);
    void addTrail(NodeAddress dest, NodeAddress over, double delta);
    void trailWritten(NodeAddress dest, NodeAddress over, bool existed);
    void reportState();
    AntDecision decided(AntDecision decision, NodeAddress dest);
    NodeAddress bestHop(NodeAddress dest) const;
    void layTrail(NodeAddress toward, NodeAddress over, std::uint8_t hops);
    void reinforce(NodeAddress toward, NodeAddress over);
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
    swarm::core::IRouterObserver* observer_ = nullptr;
    std::optional<std::size_t> reportedEntries_;  ///< table size the observer last heard
};

}  // namespace ara::core

#endif  // ARA_CORE_ROUTER_H
