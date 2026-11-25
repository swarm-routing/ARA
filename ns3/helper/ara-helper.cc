// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "ara-helper.h"

#include "ns3/ara-routing-protocol.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/ipv4.h"
#include "ns3/loopback-net-device.h"
#include "ns3/prio-queue-disc.h"
#include "ns3/string.h"
#include "ns3/traffic-control-helper.h"
#include "ns3/traffic-control-layer.h"

namespace ns3 {

namespace {

/// The ARA protocol on `node`, alone or in a list; null when there is none.
Ptr<ara::RoutingProtocol> AraOn(Ptr<Node> node) {
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    if (!ipv4) return nullptr;
    Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol();
    if (Ptr<ara::RoutingProtocol> ara = DynamicCast<ara::RoutingProtocol>(rp)) return ara;
    if (Ptr<Ipv4ListRouting> list = DynamicCast<Ipv4ListRouting>(rp)) {
        for (uint32_t i = 0; i < list->GetNRoutingProtocols(); ++i) {
            int16_t priority = 0;
            if (Ptr<ara::RoutingProtocol> ara =
                    DynamicCast<ara::RoutingProtocol>(list->GetRoutingProtocol(i, priority))) {
                return ara;
            }
        }
    }
    return nullptr;
}

}  // namespace

AraHelper::AraHelper() : SwarmRoutingHelper("ns3::ara::RoutingProtocol") {}

AraHelper* AraHelper::Copy() const {
    return new AraHelper(*this);
}

void AraHelper::Install(const NodeContainer& nodes) const {
    SwarmRoutingHelper::Install(nodes);
    InstallAntPriority(nodes);
}

uint32_t InstallAntPriority(const NodeContainer& nodes) {
    // #137: band 0 for ants (priority 7, ara::kAntPriority, the priomap) and for
    // data flagged Backtrack or DuplicateError (ara::DataFlagFilter); band 1
    // for the rest. Each band an FqCoDelQueueDisc with ns-3's defaults, the
    // default root queue disc Ipv4AddressHelper::Assign would install.
    TrafficControlHelper tch;
    const uint16_t root = tch.SetRootQueueDisc("ns3::PrioQueueDisc", "Priomap",
                                               StringValue("1 1 1 1 1 1 1 0 1 1 1 1 1 1 1 1"));
    const TrafficControlHelper::ClassIdList bands =
        tch.AddQueueDiscClasses(root, 2, "ns3::QueueDiscClass");
    tch.AddChildQueueDisc(root, bands[0], "ns3::FqCoDelQueueDisc");
    tch.AddChildQueueDisc(root, bands[1], "ns3::FqCoDelQueueDisc");
    tch.AddPacketFilter(root, "ns3::ara::DataFlagFilter");

    uint32_t installed = 0;
    for (auto it = nodes.Begin(); it != nodes.End(); ++it) {
        Ptr<ara::RoutingProtocol> ara = AraOn(*it);
        if (!ara || !ara->PrioritizesAnts()) continue;
        Ptr<TrafficControlLayer> tc = (*it)->GetObject<TrafficControlLayer>();
        for (uint32_t d = 0; d < (*it)->GetNDevices(); ++d) {
            Ptr<NetDevice> dev = (*it)->GetDevice(d);
            if (DynamicCast<LoopbackNetDevice>(dev)) continue;
            if (tc && tc->GetRootQueueDiscOnDevice(dev)) continue;  // one already chosen
            tch.Install(dev);
            ++installed;
        }
    }
    return installed;
}

}  // namespace ns3
