// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// ARA's per-node state and decision logic, written fresh from Güneş, Sorges &
// Bouazizi 2002 ([G02]) and docs/fidelity.md (golden rule 12). This first
// slice is route discovery ([G02] §3.1): the FANT flood, the BANT answer, the
// pheromone trails they lay, evaporation and the deterministic next-hop
// choice, and the discovery retries of the 2026-11-10 decisions (#4, #5).
// The second is route maintenance by data ([G02] §3.2): every data hop adds
// Δϕ toward both ends. The third is loop prevention and route failure
// ([G02] §3.2, §3.3): duplicate data goes back with DUPLICATE_ERROR and its
// sender drops that link; a failed link is dropped and the packet takes
// another, or backtracks to the previous hop, up to the source, which starts
// a new discovery (fidelity rows "Loop prevention", "On a failed link", "No
// other link").
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

/// A data packet's identity for duplicate detection: fidelity "Data packet
/// 'sequence number' used for duplicate detection", the key (source,
/// destination, IP protocol, IP identification) of #4 and #5.
struct DataKey {
    NodeAddress source = swarm::core::kInvalidAddress;
    NodeAddress dest = swarm::core::kInvalidAddress;
    std::uint8_t protocol = 0;
    std::uint16_t id = 0;
    auto operator<=>(const DataKey&) const = default;
};

/// What to do with a data packet whose next hop failed, or that came back.
struct FailureDecision {
    AntDecision decision;    ///< Unicast to the next hop, or Drop
    bool backtrack = false;  ///< the next hop is the previous one: send it flagged
};

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

    /// A data packet `key` arrived from `prev` (kInvalidAddress: this node
    /// originates it). False when the key was seen before: a loop ([G02]
    /// §3.2), and the packet goes back to `prev` with DUPLICATE_ERROR. The
    /// first sighting records `prev` for backtracking, in a history bounded by
    /// historyCapacity whose entries expire after historyLifetime (fidelity
    /// "Ant and data dedup history size", #4, #5). A
    /// packet that comes back flagged Backtrack is no duplicate: do not call
    /// this for it, call onRouteFailure.
    bool admitData(const DataKey& key, NodeAddress prev);
    /// A data packet for `dest` came back from `from` with DUPLICATE_ERROR:
    /// that link is deactivated, its ϕ set to 0 ([G02] §3.2).
    void onDuplicateError(NodeAddress dest, NodeAddress from);
    /// Data packet `key` could not go over `failed`: the MAC gave up on it, or
    /// `failed` sent it back flagged Backtrack ([G02] §3.3). The link's ϕ goes
    /// to 0 and the packet takes another trail if one exists; otherwise it
    /// backtracks to the hop it came from (fidelity "No other link", #15). At
    /// the source, with no trail left, a discovery starts and the packet is
    /// dropped (no buffering, #4); a relay that no longer knows the previous
    /// hop (evicted from the history) drops it.
    /// `cameFrom`: the hop the caller knows the packet came from (the MAC's
    /// report carries it), used when the history no longer holds the key, so
    /// a packet that waited at the MAC past the history's cap still finds its
    /// way back; kInvalidAddress when the caller does not know.
    FailureDecision onRouteFailure(const DataKey& key, NodeAddress failed,
                                   NodeAddress cameFrom = swarm::core::kInvalidAddress);

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

    /// A hop kept in a history, with the time it was recorded.
    struct TimedHop {
        NodeAddress hop;
        Time at;
    };

    /// (FANT source, sequence) -> the neighbour the FANT came from: the path
    /// the BANT is unicast back along (fidelity "How the BANT travels back").
    /// Recorded with the FANT history and bounded by the same cap, oldest
    /// evicted first (org rule 7), with the same lifetime: an entry recorded
    /// at t reads as absent from t + lifetime on.
    class ReversePointers {
    public:
        ReversePointers(std::size_t capacity, Time lifetime);
        void record(NodeAddress source, std::uint32_t sequence, NodeAddress previousHop, Time now);
        NodeAddress find(NodeAddress source, std::uint32_t sequence, Time now) const;
        std::size_t size() const { return hops_.size(); }

    private:
        using Key = std::pair<NodeAddress, std::uint32_t>;
        std::size_t capacity_;
        Time lifetime_;
        std::map<Key, TimedHop> hops_;
        std::deque<Key> order_;
    };

    /// DataKey -> the hop the packet came from, kept for backtracking; the
    /// data dedup history, bounded by historyCapacity, oldest evicted first,
    /// each entry expired from its record time + lifetime on.
    class DataHistory {
    public:
        DataHistory(std::size_t capacity, Time lifetime);
        /// False when `key` is already there and not expired at `now` (its
        /// first hop is kept); an expired entry is recorded afresh.
        bool record(const DataKey& key, NodeAddress previousHop, Time now);
        /// The hop `key` came from; kInvalidAddress when unknown, expired at
        /// `now` or originated here.
        NodeAddress find(const DataKey& key, Time now) const;
        std::size_t size() const { return hops_.size(); }

    private:
        std::size_t capacity_;
        Time lifetime_;
        std::map<DataKey, TimedHop> hops_;
        std::deque<DataKey> order_;
    };
    void age();
    /// Removes (dest, over), telling the observer when it existed.
    void dropTrail(NodeAddress dest, NodeAddress over);
    /// The sink, after telling the observer (an ant is kAntWireSize bytes).
    void emit(const AntDecision& decision);
    /// Table writes that tell the observer of a new entry and a new size.
    void setTrail(NodeAddress dest, NodeAddress over, double value);
    void addTrail(NodeAddress dest, NodeAddress over, double delta);
    void trailWritten(NodeAddress dest, NodeAddress over, bool existed);
    void reportState();
    AntDecision decided(AntDecision decision, NodeAddress dest);
    NodeAddress bestHop(NodeAddress dest) const;
    /// bestHop() without the trail over `except`.
    NodeAddress bestHopExcept(NodeAddress dest, NodeAddress except) const;
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
    DataHistory dataHistory_;
    std::map<NodeAddress, Discovery> discoveries_;
    std::uint32_t nextSequence_ = 1;
    Time lastAged_;
    swarm::core::IRouterObserver* observer_ = nullptr;
    std::optional<std::size_t> reportedEntries_;  ///< table size the observer last heard
    /// age()'s scratch: the (destination, neighbour) entries about to expire.
    std::vector<std::pair<NodeAddress, NodeAddress>> expiring_;
};

}  // namespace ara::core

#endif  // ARA_CORE_ROUTER_H
