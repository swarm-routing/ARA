// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Installs ARA on nodes: swarm-core's SwarmRoutingHelper for
// ns3::ara::RoutingProtocol, so `Install` also adds the link monitors that
// apply to each node.
//
//   AraHelper ara;
//   ara.Install(nodes);           // IPv4 stack + ARA + monitors
//   ...assign addresses...
//   ara.AssignStreams(nodes, 0);  // before Simulator::Run
#ifndef ARA_HELPER_H
#define ARA_HELPER_H

#include "ns3/swarm-routing-helper.h"

namespace ns3 {

class AraHelper : public SwarmRoutingHelper {
public:
    AraHelper();
    AraHelper* Copy() const override;
};

}  // namespace ns3

#endif  // ARA_HELPER_H
