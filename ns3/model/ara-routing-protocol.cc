// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "ara-routing-protocol.h"

#include "ara-header.h"
#include "ara-router.h"

#include "ns3/abort.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/integer.h"
#include "ns3/ipv4-interface-address.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/log.h"
#include "ns3/net-device.h"
#include "ns3/socket.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <limits>
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
        // The core's constants (#108): each default is core::Config's, each
        // value is checked against the range the core assumes. Times are in
        // seconds, as in the core (docs/fidelity.md "Parameters").
        const core::Config d{};
        const double positive = std::numeric_limits<double>::min();
        TypeId t =
            TypeId("ns3::ara::RoutingProtocol")
                .SetParent<SwarmRoutingBase>()
                .SetGroupName("Ara")
                .AddConstructor<RoutingProtocol>()
                .AddAttribute("BufferDuringDiscovery",
                              "Hold data with no trail while this node's discovery for its "
                              "destination runs (core Config bufferDuringDiscovery; off: "
                              "dropped, as [G02]).",
                              BooleanValue(d.bufferDuringDiscovery),
                              MakeBooleanAccessor(&RoutingProtocol::m_buffer), MakeBooleanChecker())
                .AddAttribute("StatisticalPathSelection",
                              "Data takes neighbour j with probability phi_j / sum phi, the "
                              "previous hop left out while another trail exists (core Config "
                              "statisticalPathSelection, [GS03] 2.2 and 5.2, #135; off: the "
                              "highest phi, as [G02]).",
                              BooleanValue(d.statisticalPathSelection),
                              MakeBooleanAccessor(&RoutingProtocol::m_statistical),
                              MakeBooleanChecker())
                .AddAttribute("FloodBackwardAnts",
                              "The destination broadcasts its BANT and every node relays it once, "
                              "jittered, like a FANT (core Config floodBackwardAnts, [GS03] 4.5, "
                              "#136; off: unicast back along the FANT's path, as [G02]).",
                              BooleanValue(d.floodBackwardAnts),
                              MakeBooleanAccessor(&RoutingProtocol::m_floodBants),
                              MakeBooleanChecker())
                .AddAttribute("OneRetryOnFailure",
                              "A node takes one other trail per packet whose link failed; a "
                              "second failure there goes back to the previous hop (core Config "
                              "oneRetryOnFailure, [GS03] 4.3, #138; off: every failure takes the "
                              "next trail, as [G02]).",
                              BooleanValue(d.oneRetryOnFailure),
                              MakeBooleanAccessor(&RoutingProtocol::m_oneRetry),
                              MakeBooleanChecker())
                .AddAttribute("PrioritizeAnts",
                              "FANTs, BANTs and data flagged Backtrack or DuplicateError go "
                              "ahead in the IP-level queue: a PrioQueueDisc with two FqCoDel "
                              "bands, installed by AraHelper::Install or the bench arm ([GS03] "
                              "4.5, #137; an adapter switch: off, no priority, the default "
                              "queue disc).",
                              BooleanValue(false),
                              MakeBooleanAccessor(&RoutingProtocol::m_prioritizeAnts),
                              MakeBooleanChecker())
                .AddAttribute("DataAsFant",
                              "A source's packet with no trail goes out as a FANT carrying it, "
                              "every one while the discovery runs, and the destination delivers "
                              "it (core Config dataAsFant, [GS03] 4.5 on the 2002 version, #132; "
                              "off: dropped, or held with BufferDuringDiscovery).",
                              BooleanValue(d.dataAsFant),
                              MakeBooleanAccessor(&RoutingProtocol::m_dataAsFant),
                              MakeBooleanChecker())
                .AddAttribute("EvaporationRate",
                              "Evaporation rate q of eq. 2, phi := (1 - q) phi per "
                              "EvaporationPeriod, in (0, 1] (core Config evaporationRate).",
                              DoubleValue(d.evaporationRate),
                              MakeDoubleAccessor(&RoutingProtocol::m_evaporationRate),
                              MakeDoubleChecker<double>(positive, 1.0))
                .AddAttribute("EvaporationPeriod",
                              "The period q applies to, in seconds, > 0; evaluated "
                              "continuously (core Config evaporationPeriod).",
                              DoubleValue(d.evaporationPeriod),
                              MakeDoubleAccessor(&RoutingProtocol::m_evaporationPeriod),
                              MakeDoubleChecker<double>(positive))
                .AddAttribute("Deposit",
                              "Pheromone a data packet adds on every hop, toward both ends, "
                              "> 0 (core Config deposit).",
                              DoubleValue(d.deposit),
                              MakeDoubleAccessor(&RoutingProtocol::m_deposit),
                              MakeDoubleChecker<double>(positive))
                .AddAttribute("ExpiryThreshold",
                              "A trail below this pheromone is gone, >= 0 (core Config "
                              "expiryThreshold).",
                              DoubleValue(d.expiryThreshold),
                              MakeDoubleAccessor(&RoutingProtocol::m_expiryThreshold),
                              MakeDoubleChecker<double>(0.0))
                .AddAttribute("AntTtl",
                              "Maximum hops of a FANT; the BANT carries the same (core Config "
                              "antTtl).",
                              UintegerValue(d.antTtl),
                              MakeUintegerAccessor(&RoutingProtocol::m_antTtl),
                              MakeUintegerChecker<uint8_t>(1))
                .AddAttribute("RelayJitterMax",
                              "A FANT relay waits U[0, this) seconds, >= 0 (core Config "
                              "relayJitterMax).",
                              DoubleValue(d.relayJitterMax),
                              MakeDoubleAccessor(&RoutingProtocol::m_relayJitterMax),
                              MakeDoubleChecker<double>(0.0))
                .AddAttribute("DiscoveryTimeout",
                              "Seconds before a FANT with no BANT is re-sent, doubling per "
                              "retry, > 0 (core Config discoveryTimeout).",
                              DoubleValue(d.discoveryTimeout),
                              MakeDoubleAccessor(&RoutingProtocol::m_discoveryTimeout),
                              MakeDoubleChecker<double>(positive))
                .AddAttribute("MaxDiscoveryRetries",
                              "FANT retries, each only if data for the destination arrived "
                              "since the last FANT, >= 0 (core Config maxDiscoveryRetries).",
                              IntegerValue(d.maxDiscoveryRetries),
                              MakeIntegerAccessor(&RoutingProtocol::m_maxDiscoveryRetries),
                              MakeIntegerChecker<int32_t>(0))
                .AddAttribute("HistoryCapacity",
                              "Entries in each ant and data dedup history, oldest evicted "
                              "first, >= 1 (core Config historyCapacity).",
                              UintegerValue(d.historyCapacity),
                              MakeUintegerAccessor(&RoutingProtocol::m_historyCapacity),
                              MakeUintegerChecker<uint32_t>(1))
                .AddAttribute("HistoryLifetime",
                              "Seconds a dedup history entry lives, > 0 (core Config "
                              "historyLifetime).",
                              DoubleValue(d.historyLifetime),
                              MakeDoubleAccessor(&RoutingProtocol::m_historyLifetime),
                              MakeDoubleChecker<double>(positive))
                .AddAttribute("PheromoneCapacity",
                              "(destination, neighbour) entries the pheromone table holds, "
                              ">= 1 (core Config pheromoneCapacity).",
                              UintegerValue(d.pheromoneCapacity),
                              MakeUintegerAccessor(&RoutingProtocol::m_pheromoneCapacity),
                              MakeUintegerChecker<uint32_t>(1))
                .AddAttribute("MaxDiscoveries",
                              "Destinations a node discovers at once, >= 1 (core Config "
                              "maxDiscoveries).",
                              UintegerValue(d.maxDiscoveries),
                              MakeUintegerAccessor(&RoutingProtocol::m_maxDiscoveries),
                              MakeUintegerChecker<uint32_t>(1))
                .AddTraceSource("AntTx", "An ant this node sent (type byte, broadcast).",
                                MakeTraceSourceAccessor(&RoutingProtocol::m_antTx),
                                "ns3::ara::RoutingProtocol::AntTxCallback");
        SwarmRoutingBase::SetKindNames(t, core::kAntKindNames);
        return t;
    }();
    return tid;
}

// The attributes' values are set when the object is built through its TypeId
// (CreateObject, an ObjectFactory); these are the same defaults for any other
// construction: core::Config's (#108).
RoutingProtocol::RoutingProtocol()
    : m_buffer(core::Config{}.bufferDuringDiscovery),
      m_statistical(core::Config{}.statisticalPathSelection),
      m_floodBants(core::Config{}.floodBackwardAnts), m_oneRetry(core::Config{}.oneRetryOnFailure),
      m_prioritizeAnts(false), m_dataAsFant(core::Config{}.dataAsFant),
      m_evaporationRate(core::Config{}.evaporationRate),
      m_evaporationPeriod(core::Config{}.evaporationPeriod), m_deposit(core::Config{}.deposit),
      m_expiryThreshold(core::Config{}.expiryThreshold), m_antTtl(core::Config{}.antTtl),
      m_relayJitterMax(core::Config{}.relayJitterMax),
      m_discoveryTimeout(core::Config{}.discoveryTimeout),
      m_maxDiscoveryRetries(core::Config{}.maxDiscoveryRetries),
      m_historyCapacity(static_cast<uint32_t>(core::Config{}.historyCapacity)),
      m_historyLifetime(core::Config{}.historyLifetime),
      m_pheromoneCapacity(static_cast<uint32_t>(core::Config{}.pheromoneCapacity)),
      m_maxDiscoveries(static_cast<uint32_t>(core::Config{}.maxDiscoveries)) {}

RoutingProtocol::~RoutingProtocol() = default;

void RoutingProtocol::DoInitialize() {
    // The interfaces are up by now (addresses are assigned before the
    // simulation starts), so the node's address is known. The protocol
    // constants are the attributes' values (#108): core::Config's defaults,
    // each sourced in docs/fidelity.md, unless a script set them.
    const swarm::core::NodeAddress self = ToNodeAddress(MainAddress());
    core::Config config;
    config.evaporationRate = m_evaporationRate;
    config.evaporationPeriod = m_evaporationPeriod;
    config.deposit = m_deposit;
    config.expiryThreshold = m_expiryThreshold;
    config.antTtl = m_antTtl;
    config.relayJitterMax = m_relayJitterMax;
    config.discoveryTimeout = m_discoveryTimeout;
    config.maxDiscoveryRetries = m_maxDiscoveryRetries;
    config.historyCapacity = m_historyCapacity;
    config.historyLifetime = m_historyLifetime;
    config.pheromoneCapacity = m_pheromoneCapacity;
    config.maxDiscoveries = m_maxDiscoveries;
    config.bufferDuringDiscovery = m_buffer;
    config.statisticalPathSelection = m_statistical;
    config.floodBackwardAnts = m_floodBants;
    config.oneRetryOnFailure = m_oneRetry;
    config.dataAsFant = m_dataAsFant;
    m_router = std::make_unique<core::Router>(
        self, config, m_clock, Rng(), Timers(), [this](const core::AntDecision& d) {
            if (d.message) {
                m_antTx(static_cast<uint8_t>(d.message->type),
                        d.action == swarm::core::RouteAction::Broadcast);
            }
            // With PrioritizeAnts (#137) the ant carries kAntPriority, which
            // the UDP send keeps (the control socket's own priority is 0)
            // and the PrioQueueDisc's priomap sends to band 0.
            // With DataAsFant (#132), a FANT this node relays goes with the
            // data packet it carried when it arrived.
            const bool done = Execute(d, [this](const core::AntMessage& ant) {
                if (m_dataAsFant && ant.type == core::AntType::Forward) {
                    const auto it = m_carried.find({ant.source, ant.sequence});
                    if (it != m_carried.end()) {
                        Ptr<Packet> p = it->second;
                        m_carried.erase(it);
                        p->AddHeader(AntHeader(ant));
                        return TagAnt(p);
                    }
                }
                return TagAnt(MakeAntPacket(ant));
            });
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
    // The Ipv4 holds this protocol: dropping our pointer breaks the cycle
    // (as the base does with its own), and the carried packets go too.
    m_ipv4Here = nullptr;
    m_carried.clear();
    m_carriedOrder.clear();
    m_delivered.clear();
    m_deliveredOrder.clear();
    m_pendingFants.clear();
}

void RoutingProtocol::DoNodeShutdown() {
    // #100: no leaving message in ARA; the same clearing as a crash.
    DoNodeCrash();
}

void RoutingProtocol::DoNodeCrash() {
    // #100: a restarted node keeps none of its earlier trails or histories.
    if (m_router) {
        m_router->reset();
    }
    m_transitNoTrail.reset();
    // DataAsFant (#132): a restarted node carries no FANT's data either.
    m_pendingFants.clear();
    m_carried.clear();
    m_carriedOrder.clear();
    m_delivered.clear();
    m_deliveredOrder.clear();
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
        if (d.action == swarm::core::RouteAction::Broadcast && d.message) {
            // DataAsFant (#132): no route here, so the base defers the
            // packet and calls StartDiscovery, which sends it in this FANT.
            std::deque<uint32_t>& pending = m_pendingFants[dest];
            pending.push_back(d.message->sequence);
            while (pending.size() > m_historyCapacity) {
                pending.pop_front();  // org rule 7
            }
            return Ipv4Address();
        }
        return d.action == swarm::core::RouteAction::Unicast ? ToIpv4Address(d.nextHop)
                                                             : Ipv4Address();
    }
    // The previous hop goes with it: with StatisticalPathSelection the draw
    // leaves it out while another trail exists (#135); off, it is unused.
    const core::AntDecision d = m_router->onTransitData(dest, ToNodeAddress(prev));
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
    if (m_router && m_dataAsFant) {
        SendDataFants(dst);  // #132; takes precedence over buffering
        return;
    }
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
    // With DataAsFant (#132) a FANT may carry a data packet behind its
    // header: its presence is read from the length. Anything else is decoded
    // as exactly one ant, as before.
    std::optional<core::AntMessage> ant;
    Ptr<Packet> carried;
    if (m_dataAsFant && p->GetSize() > core::kAntWireSize) {
        Ptr<Packet> c = p->Copy();
        AntHeader h;
        c->RemoveHeader(h);
        if (h.IsValid() && h.Get().type == core::AntType::Forward) {
            ant = h.Get();
            carried = c;
        }
    } else {
        ant = DecodeAntPacket(p);
    }
    if (!ant) {
        NS_LOG_DEBUG("dropped a malformed ant from " << from);
        return;
    }
    if (carried && ant->source != m_router->self()) {
        const FantKey key{ant->source, ant->sequence};
        if (ant->destination == m_router->self()) {
            // The destination delivers the data once per FANT.
            if (m_delivered.insert(key).second) {
                m_deliveredOrder.push_back(key);
                while (m_deliveredOrder.size() > m_historyCapacity) {
                    m_delivered.erase(m_deliveredOrder.front());
                    m_deliveredOrder.pop_front();
                }
                DeliverCarried(carried, from);
            }
        } else if (m_carried.emplace(key, carried).second) {
            // A relay keeps it for the core's relay of this FANT (the first
            // copy only, as the core relays only that one).
            m_carriedOrder.push_back(key);
            while (m_carriedOrder.size() > m_historyCapacity) {
                m_carried.erase(m_carriedOrder.front());
                m_carriedOrder.pop_front();
            }
        }
    }
    m_router->onAnt(*ant, ToNodeAddress(from));
    // A FANT lays a trail toward its source, a BANT toward its source (the
    // destination of the discovery): data held for it can go (buffering, #5).
    ReleaseIfRouted(ToIpv4Address(ant->source));
}

void RoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4) {
    m_ipv4Here = ipv4;
    SwarmRoutingBase::SetIpv4(ipv4);
}

Ptr<Packet> RoutingProtocol::TagAnt(Ptr<Packet> p) const {
    // With PrioritizeAnts (#137) the ant carries kAntPriority, which the UDP
    // send keeps (the control socket's own priority is 0) and the
    // PrioQueueDisc's priomap sends to band 0.
    if (m_prioritizeAnts) {
        SocketPriorityTag tag;
        tag.SetPriority(kAntPriority);
        p->ReplacePacketTag(tag);
    }
    return p;
}

void RoutingProtocol::SendDataFants(Ipv4Address dst) {
    // #132: each of this node's own packets the base deferred for dst goes
    // out in the FANT onLocalData chose for it, in order: the ant header,
    // then the IP header and payload. Anything else held for dst (a transit
    // packet with no trail, or an own packet with no FANT left, which the
    // discovery cap refused) is dropped, as without the switch.
    const swarm::core::NodeAddress dest = ToNodeAddress(dst);
    std::deque<uint32_t>& pending = m_pendingFants[dest];
    std::vector<DeferredPacket> rest;
    for (DeferredPacket& e : GetDeferredQueue()->Release(dst)) {
        if (pending.empty() || !IsMyAddress(e.header.GetSource())) {
            rest.push_back(std::move(e));
            continue;
        }
        const core::AntMessage fant{
            core::AntType::Forward, m_router->self(), dest, pending.front(), 0, m_antTtl};
        pending.pop_front();
        Ptr<Packet> p = e.packet->Copy();
        p->AddHeader(e.header);
        p->AddHeader(AntHeader(fant));
        m_antTx(static_cast<uint8_t>(fant.type), true);
        BroadcastControl(TagAnt(p));
    }
    if (pending.empty()) {
        m_pendingFants.erase(dest);
    }
    if (!rest.empty()) {
        GetDeferredQueue()->Requeue(std::move(rest));
        DiscardDeferred(dst);
    }
}

void RoutingProtocol::DeliverCarried(Ptr<Packet> ip, Ipv4Address from) {
    // #132 (item 3 as amended 2026-11-25): Ipv4L3Protocol::LocalDeliver is
    // private, so the carried packet enters this node's stack the way a frame
    // does, on the interface whose subnet holds `from`; RouteInput then
    // delivers it (the base's OnDataReceived, the core's Deliver).
    Ptr<Ipv4L3Protocol> l3 = DynamicCast<Ipv4L3Protocol>(m_ipv4Here);
    if (!l3) return;
    for (uint32_t i = 0; i < l3->GetNInterfaces(); ++i) {
        for (uint32_t a = 0; a < l3->GetNAddresses(i); ++a) {
            const Ipv4InterfaceAddress ifa = l3->GetAddress(i, a);
            if (ifa.GetLocal() == Ipv4Address::GetLoopback() ||
                ifa.GetLocal().CombineMask(ifa.GetMask()) != from.CombineMask(ifa.GetMask())) {
                continue;
            }
            Ptr<NetDevice> dev = l3->GetNetDevice(i);
            l3->Receive(dev, ip, Ipv4L3Protocol::PROT_NUMBER, dev->GetAddress(), dev->GetAddress(),
                        NetDevice::PACKET_HOST);
            return;
        }
    }
}

}  // namespace ara
}  // namespace ns3
