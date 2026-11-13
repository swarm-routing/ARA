// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// ARA's ns-3 adapter: an ns3::SwarmRoutingBase that hosts one core::Router
// (core/ara-router.h) per node and carries out its decisions (golden rule 10).
// It only converts: ants between AntHeader bytes and core::AntMessage, IPv4
// addresses to core addresses, and the router's RouteDecisions into
// SwarmRoutingBase's sends. Time, randomness and timers are swarm-core's ports
// over ns-3 (Simulator::Now, a stream AssignStreams pins, Simulator::Schedule),
// so a fixed seed gives byte-identical runs (org rule 5). Written fresh on
// swarm-core; the control port is the one AntHocNet's `ara` arm used.
//
// What the core decides, the adapter does:
//  - data this node originates: core::Router::onLocalData. A Unicast is the
//    next hop. A Drop (no trail) has already started a discovery or counted
//    as demand for its retry; the packet is dropped, never held (fidelity
//    "Data packets waiting for a route": no buffering in core/ or here). The
//    base sends a packet with no next hop to its deferred queue and calls
//    StartDiscovery, which discards it at once through its error callback.
//    With the BufferDuringDiscovery attribute (core Config
//    bufferDuringDiscovery, off by default; #5), the core answers Queue while
//    its discovery runs and StartDiscovery leaves the packet in the base's
//    queue (MaxPackets, MaxAge). An ant or a data packet that lays a trail
//    toward a destination with held packets releases them
//    (ReleaseIfRouted); a discovery that ends with no trail sends the core's
//    DiscardPending, which drops them.
//  - data in transit: core::Router::nextHop, the highest-pheromone neighbour;
//    none drops the packet the same way.
//  - route maintenance by data ([G02] §3.2): every data packet this node
//    hands to a next hop (OnDataForwarded, at the source and at each relay)
//    goes to core::Router::onDataSent, and every data packet that arrives
//    from a neighbour (OnDataReceived, at a relay and at the destination) to
//    core::Router::onDataReceived.
//  - loop prevention and route failure ([G02] §3.2, §3.3): every arriving
//    data packet goes to core::Router::admitData; a duplicate goes back to
//    its sender flagged DUPLICATE_ERROR, whose arrival drops that link
//    (onDuplicateError). A packet the Wi-Fi MAC gave up on
//    (OnDataUndelivered) or that came back flagged Backtrack goes to
//    onRouteFailure, whose answer ResendData carries out: another trail, or
//    back to the previous hop flagged Backtrack. The flags are a packet tag
//    (DataFlagTag, ara-header.h).
//  - ants: the core's Broadcast and Unicast decisions, as one-hop UDP sends.
//  - the observer: the core reports through SwarmRoutingBase::Observer()
//    (ControlTx/ControlRx/RouteChanged/DataDecision/StateSize trace sources,
//    GetRouterStats), and the FANT/BANT kind names are registered with the
//    TypeId. Transit data asks core::Router::onTransitData; at the
//    destination, OnDataReceived reports the core's Deliver.
#ifndef ARA_ROUTING_PROTOCOL_H
#define ARA_ROUTING_PROTOCOL_H

#include "ns3/swarm-ns3-ports.h"
#include "ns3/swarm-routing-base.h"
#include "ns3/traced-callback.h"

#include <cstdint>
#include <memory>

namespace ara::core {
class Router;
struct FailureDecision;
}  // namespace ara::core

namespace ns3 {
namespace ara {

/// ARA's simulator-free core (core/), from inside ns3::ara.
namespace core = ::ara::core;

class RoutingProtocol : public SwarmRoutingBase {
public:
    /// UDP port of FANTs and BANTs (AntHocNet's `ara` arm: 5541).
    static constexpr uint16_t kAraPort = 5541;

    static TypeId GetTypeId();
    RoutingProtocol();
    ~RoutingProtocol() override;

    /// The neighbour the core would hand data for `dst` to now, or
    /// Ipv4Address() when it has no trail (or the protocol has not started).
    Ipv4Address NextHop(Ipv4Address dst);

    /// An ant this node sent: (core::AntType as its wire byte, broadcast).
    typedef void (*AntTxCallback)(uint8_t type, bool broadcast);

protected:
    void DoInitialize() override;
    void DoDispose() override;

    uint16_t ControlPort() const override { return kAraPort; }
    Ipv4Address SelectNextHop(Ipv4Address dst, Ipv4Address src, Ipv4Address prev,
                              Ptr<const Packet> p) override;
    void StartDiscovery(Ipv4Address dst) override;
    void RecvControl(Ptr<Packet> p, Ipv4Address from, Ipv4Address receiver) override;
    bool OnDataReceived(Ptr<const Packet> p, const Ipv4Header& header, Ipv4Address prev) override;
    void OnDataForwarded(Ptr<const Packet> p, const Ipv4Header& header, Ipv4Address next) override;
    void OnDataUndelivered(Ptr<const Packet> p, const Ipv4Header& header, Ipv4Address failedNext,
                           Ipv4Address prev) override;

private:
    /// The core's clock port. Clock() is const and the router takes a
    /// mutable reference; SwarmClock is stateless, so this one reads the same.
    SwarmClock m_clock;
    std::unique_ptr<core::Router> m_router;
    /// The BufferDuringDiscovery attribute: core Config bufferDuringDiscovery.
    bool m_buffer = false;
    /// With buffering on: sends the packets held for `dst` when the core now
    /// has a trail toward it. Asks the core nothing when none are held.
    void ReleaseIfRouted(Ipv4Address dst);
    /// Sends a packet as onRouteFailure decided: to another trail, or back to
    /// the previous hop flagged Backtrack; nothing for a Drop.
    void CarryOut(const core::FailureDecision& d, Ptr<const Packet> p, const Ipv4Header& header);
    TracedCallback<uint8_t, bool> m_antTx;
};

}  // namespace ara
}  // namespace ns3

#endif  // ARA_ROUTING_PROTOCOL_H
