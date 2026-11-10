// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "ara-routing-protocol.h"

#include "ara-header.h"
#include "ara-router.h"

#include "ns3/abort.h"
#include "ns3/log.h"

namespace ns3 {
namespace ara {

NS_LOG_COMPONENT_DEFINE("AraRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(RoutingProtocol);

TypeId RoutingProtocol::GetTypeId() {
    static TypeId tid =
        TypeId("ns3::ara::RoutingProtocol")
            .SetParent<SwarmRoutingBase>()
            .SetGroupName("Ara")
            .AddConstructor<RoutingProtocol>()
            .AddTraceSource("AntTx", "An ant this node sent (type byte, broadcast).",
                            MakeTraceSourceAccessor(&RoutingProtocol::m_antTx),
                            "ns3::ara::RoutingProtocol::AntTxCallback");
    return tid;
}

RoutingProtocol::RoutingProtocol() = default;

RoutingProtocol::~RoutingProtocol() = default;

void RoutingProtocol::DoInitialize() {
    // The interfaces are up by now (addresses are assigned before the
    // simulation starts), so the node's address is known. The protocol
    // constants are core::Config's defaults, each sourced in docs/fidelity.md.
    const swarm::core::NodeAddress self = ToNodeAddress(MainAddress());
    m_router = std::make_unique<core::Router>(
        self, core::Config{}, m_clock, Rng(), Timers(), [this](const core::AntDecision& d) {
            NS_ASSERT_MSG(d.message, "the ARA core sends ants only");
            m_antTx(static_cast<uint8_t>(d.message->type),
                    d.action == swarm::core::RouteAction::Broadcast);
            const bool done = Execute(d, MakeAntPacket);
            NS_ASSERT_MSG(done, "the ARA core sends ants only by Broadcast or Unicast");
            (void)done;
        });
    SwarmRoutingBase::DoInitialize();
}

void RoutingProtocol::DoDispose() {
    // The base cancels the core's pending timers first; then the router goes.
    SwarmRoutingBase::DoDispose();
    m_router.reset();
}

Ipv4Address RoutingProtocol::NextHop(Ipv4Address dst) {
    return m_router ? ToIpv4Address(m_router->nextHop(ToNodeAddress(dst))) : Ipv4Address();
}

Ipv4Address RoutingProtocol::SelectNextHop(Ipv4Address dst, Ipv4Address /*src*/, Ipv4Address prev,
                                           Ptr<const Packet> /*p*/) {
    // A broadcast or multicast address is no core address (255.255.255.255
    // reads as kInvalidAddress): such packets are not routed.
    if (!m_router || dst.IsBroadcast() || dst.IsMulticast()) {
        return Ipv4Address();
    }
    const swarm::core::NodeAddress dest = ToNodeAddress(dst);
    if (prev == Ipv4Address()) {
        // This node's own data (RouteOutput): the core decides, and a Drop has
        // started the discovery or counted as demand for its retry.
        const core::AntDecision d = m_router->onLocalData(dest);
        return d.action == swarm::core::RouteAction::Unicast ? ToIpv4Address(d.nextHop)
                                                             : Ipv4Address();
    }
    return ToIpv4Address(m_router->nextHop(dest));
}

void RoutingProtocol::StartDiscovery(Ipv4Address dst) {
    // The core has no route for this packet and holds none (its Drop): the
    // packet the base just deferred is dropped now. A source's discovery was
    // started by onLocalData; a relay starts none ([G02] §3.3's backtracking
    // is the core's next slice).
    DiscardDeferred(dst);
}

void RoutingProtocol::RecvControl(Ptr<Packet> p, Ipv4Address from, Ipv4Address /*receiver*/) {
    if (!m_router) return;
    const auto ant = DecodeAntPacket(p);
    if (!ant) {
        NS_LOG_DEBUG("dropped a malformed ant from " << from);
        return;
    }
    m_router->onAnt(*ant, ToNodeAddress(from));
}

}  // namespace ara
}  // namespace ns3
