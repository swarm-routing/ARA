// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "ara-routing-protocol.h"

#include "ara-header.h"
#include "ara-router.h"

#include "ns3/abort.h"
#include "ns3/boolean.h"
#include "ns3/log.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace ns3 {
namespace ara {

NS_LOG_COMPONENT_DEFINE("AraRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(RoutingProtocol);

TypeId RoutingProtocol::GetTypeId() {
    // Registered with the TypeId, once, so every way of installing the
    // protocol gets the names (FANT, BANT) in its observer's readers.
    static TypeId tid = [] {
        TypeId t =
            TypeId("ns3::ara::RoutingProtocol")
                .SetParent<SwarmRoutingBase>()
                .SetGroupName("Ara")
                .AddConstructor<RoutingProtocol>()
                .AddAttribute("BufferDuringDiscovery",
                              "Hold data with no trail while this node's discovery for its "
                              "destination runs (core Config bufferDuringDiscovery; off: "
                              "dropped, as [G02]).",
                              BooleanValue(false), MakeBooleanAccessor(&RoutingProtocol::m_buffer),
                              MakeBooleanChecker())
                .AddTraceSource("AntTx", "An ant this node sent (type byte, broadcast).",
                                MakeTraceSourceAccessor(&RoutingProtocol::m_antTx),
                                "ns3::ara::RoutingProtocol::AntTxCallback");
        SwarmRoutingBase::SetKindNames(t, core::kAntKindNames);
        return t;
    }();
    return tid;
}

RoutingProtocol::RoutingProtocol() = default;

RoutingProtocol::~RoutingProtocol() = default;

void RoutingProtocol::DoInitialize() {
    // The interfaces are up by now (addresses are assigned before the
    // simulation starts), so the node's address is known. The protocol
    // constants are core::Config's defaults, each sourced in docs/fidelity.md,
    // and the one switch this adapter exposes (BufferDuringDiscovery).
    const swarm::core::NodeAddress self = ToNodeAddress(MainAddress());
    core::Config config;
    config.bufferDuringDiscovery = m_buffer;
    m_router = std::make_unique<core::Router>(
        self, config, m_clock, Rng(), Timers(), [this](const core::AntDecision& d) {
            if (d.message) {
                m_antTx(static_cast<uint8_t>(d.message->type),
                        d.action == swarm::core::RouteAction::Broadcast);
            }
            const bool done = Execute(d, MakeAntPacket);
            NS_ASSERT_MSG(done &&
                              (d.message || d.action == swarm::core::RouteAction::DiscardPending),
                          "the ARA core sends ants (Broadcast, Unicast) and DiscardPending only");
            (void)done;
        });
    m_router->setObserver(&Observer());
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
                                           Ptr<const Packet> p) {
    // A broadcast or multicast address is no core address (255.255.255.255
    // reads as kInvalidAddress): such packets are not routed.
    if (!m_router || dst.IsBroadcast() || dst.IsMulticast()) {
        return Ipv4Address();
    }
    const swarm::core::NodeAddress dest = ToNodeAddress(dst);
    if (prev == Ipv4Address()) {
        // This node's own data (RouteOutput): the core decides, and a Drop has
        // started the discovery or counted as demand for its retry. A packet
        // to this node itself (the ICMP error the stack sends for a dropped
        // packet) never leaves it: no route, no decision to report.
        if (dest == m_router->self()) {
            return Ipv4Address();
        }
        const core::AntDecision d = m_router->onLocalData(dest);
        return d.action == swarm::core::RouteAction::Unicast ? ToIpv4Address(d.nextHop)
                                                             : Ipv4Address();
    }
    const core::AntDecision d = m_router->onTransitData(dest);
    if (d.action != swarm::core::RouteAction::Unicast) {
        if (m_buffer && p) {
            m_transitNoTrail = p->GetUid();  // dropped, never held (StartDiscovery)
        }
        return Ipv4Address();
    }
    return ToIpv4Address(d.nextHop);
}

void RoutingProtocol::StartDiscovery(Ipv4Address dst) {
    // The core has no route for this packet and holds none (its Drop): the
    // packet the base just deferred is dropped now. A source's discovery was
    // started by onLocalData; a relay starts none (a packet whose link fails
    // backtracks instead, [G02] §3.3: OnDataUndelivered).
    const std::optional<uint64_t> transit = std::exchange(m_transitNoTrail, std::nullopt);
    if (m_router && m_buffer) {
        // Buffering (#5): held while this node's discovery for dst runs. The
        // loopback hop between RouteOutput and here is an event of its own,
        // so a trail may have arrived in between: send the packet then.
        const swarm::core::NodeAddress dest = ToNodeAddress(dst);
        if (m_router->nextHop(dest) != swarm::core::kInvalidAddress) {
            ReleaseDeferred(dst);
            return;
        }
        if (m_router->holds(dest)) {
            if (transit) {
                // Held: this node's own data for dst. A transit packet the
                // core dropped goes now, alone.
                DiscardTransit(dst, *transit);
            }
            return;
        }
    }
    DiscardDeferred(dst);
}

void RoutingProtocol::DiscardTransit(Ipv4Address dst, uint64_t uid) {
    // The queue drops by destination only: take the destination's packets
    // out, put the transit one back alone and discard it, then put the held
    // ones back, keeping their first enqueue time (as ReleaseDeferred puts
    // back what it cannot send).
    Ptr<DeferredPacketQueue> q = GetDeferredQueue();
    std::vector<DeferredPacket> held = q->Release(dst);
    std::vector<DeferredPacket> drop;
    const auto it = std::find_if(held.begin(), held.end(), [uid](const DeferredPacket& e) {
        return e.packet->GetUid() == uid;
    });
    if (it != held.end()) {
        drop.push_back(std::move(*it));
        held.erase(it);
    }
    q->Requeue(std::move(drop));
    q->Discard(dst);
    q->Requeue(std::move(held));
}

void RoutingProtocol::ReleaseIfRouted(Ipv4Address dst) {
    if (m_buffer && m_router && GetDeferredQueue()->Has(dst) &&
        m_router->nextHop(ToNodeAddress(dst)) != swarm::core::kInvalidAddress) {
        ReleaseDeferred(dst);
    }
}

namespace {

core::DataKey KeyOf(const Ipv4Header& header) {
    // Fidelity "Data packet 'sequence number' used for duplicate detection":
    // (source, destination, IP protocol, IP identification), #4 and #5.
    return core::DataKey{ToNodeAddress(header.GetSource()), ToNodeAddress(header.GetDestination()),
                         header.GetProtocol(), header.GetIdentification()};
}

/// A copy of `p` carrying `flag` only (a resend drops the flag it came with).
Ptr<Packet> Flagged(Ptr<const Packet> p, DataFlagTag::Flag flag) {
    Ptr<Packet> q = p->Copy();
    DataFlagTag old;
    q->RemovePacketTag(old);
    if (flag != DataFlagTag::None) {
        q->AddPacketTag(DataFlagTag(flag));
    }
    return q;
}

}  // namespace

bool RoutingProtocol::OnDataReceived(Ptr<const Packet> p, const Ipv4Header& header,
                                     Ipv4Address prev) {
    if (!m_router) return true;
    const swarm::core::NodeAddress dest = ToNodeAddress(header.GetDestination());
    switch (ReadDataFlag(p)) {
    case DataFlagTag::DuplicateError:
        // [G02] §3.2: `prev` saw this packet before; that link goes. The copy
        // is dropped: the original is still on its way.
        m_router->onDuplicateError(dest, ToNodeAddress(prev));
        return false;
    case DataFlagTag::Backtrack:
        // [G02] §3.3: `prev` had no other trail and sent the packet back.
        CarryOut(m_router->onRouteFailure(KeyOf(header), ToNodeAddress(prev)), p, header);
        return false;
    case DataFlagTag::None:
        break;
    }
    if (!m_router->admitData(KeyOf(header), ToNodeAddress(prev))) {
        // [G02] §3.2: a loop. Back to the sender with DUPLICATE_ERROR.
        ResendData(Flagged(p, DataFlagTag::DuplicateError), header, prev);
        return false;
    }
    // [G02] §3.2: the next hop strengthens the trail toward the source.
    m_router->onDataReceived(ToNodeAddress(header.GetSource()), ToNodeAddress(prev));
    ReleaseIfRouted(header.GetSource());  // a trail data laid (buffering, #5)
    // At the destination the base hands the packet up (no SelectNextHop):
    // the core reports that decision (Deliver) here. A relay's decision is
    // reported by SelectNextHop next.
    if (dest == m_router->self()) {
        (void)m_router->onTransitData(dest);
    }
    return true;
}

void RoutingProtocol::OnDataUndelivered(Ptr<const Packet> p, const Ipv4Header& header,
                                        Ipv4Address failedNext, Ipv4Address prev) {
    // [G02] §3.3: the MAC gave up on `failedNext`. The core's data history
    // knows the hop the packet came from; the base's `prev` (from the
    // previous-hop tag, 0.0.0.0 at the source) covers a key the history has
    // evicted while the packet waited at the MAC.
    if (m_router && failedNext != Ipv4Address()) {
        CarryOut(m_router->onRouteFailure(KeyOf(header), ToNodeAddress(failedNext),
                                          prev == Ipv4Address() ? swarm::core::kInvalidAddress
                                                                : ToNodeAddress(prev)),
                 p, header);
    }
}

void RoutingProtocol::CarryOut(const core::FailureDecision& d, Ptr<const Packet> p,
                               const Ipv4Header& header) {
    if (d.decision.action != swarm::core::RouteAction::Unicast) {
        return;  // Drop: at the source a discovery has started (no buffering, #4)
    }
    const Ipv4Address next = ToIpv4Address(d.decision.nextHop);
    if (!d.backtrack) {
        // Another trail toward the destination: a data hop like any other.
        m_router->onDataSent(ToNodeAddress(header.GetDestination()), d.decision.nextHop);
    }
    ResendData(Flagged(p, d.backtrack ? DataFlagTag::Backtrack : DataFlagTag::None), header, next);
}

void RoutingProtocol::OnDataForwarded(Ptr<const Packet> p, const Ipv4Header& header,
                                      Ipv4Address next) {
    // [G02] §3.2: the node handing data to `next` strengthens (D, next).
    // RouteOutput can be asked for a route with no packet (a socket probing
    // its source address): no data moves then, so nothing is deposited.
    if (m_router && p) {
        m_router->onDataSent(ToNodeAddress(header.GetDestination()), ToNodeAddress(next));
    }
}

void RoutingProtocol::RecvControl(Ptr<Packet> p, Ipv4Address from, Ipv4Address /*receiver*/) {
    if (!m_router) return;
    const auto ant = DecodeAntPacket(p);
    if (!ant) {
        NS_LOG_DEBUG("dropped a malformed ant from " << from);
        return;
    }
    m_router->onAnt(*ant, ToNodeAddress(from));
    // A FANT lays a trail toward its source, a BANT toward its source (the
    // destination of the discovery): data held for it can go (buffering, #5).
    ReleaseIfRouted(ToIpv4Address(ant->source));
}

}  // namespace ara
}  // namespace ns3
