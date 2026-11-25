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

#include "ns3/node-container.h"
#include "ns3/swarm-routing-helper.h"

#include <cstdint>

namespace ns3 {

class AraHelper : public SwarmRoutingHelper {
public:
    AraHelper();
    AraHelper* Copy() const override;

    /// SwarmRoutingHelper::Install, then InstallAntPriority: call it before
    /// the addresses are assigned, as SwarmRoutingHelper::Install.
    void Install(const NodeContainer& nodes) const;
};

/// With PrioritizeAnts on a node's ARA protocol (#137), installs the root
/// queue disc of every non-loopback device of that node that has none yet: a
/// PrioQueueDisc whose band 0 takes ants and data flagged Backtrack or
/// DuplicateError, band 1 the rest, each band an FqCoDelQueueDisc. Call it
/// after the stack is installed and before Ipv4AddressHelper::Assign (which
/// installs the default FqCoDel where none is). Nodes without the switch are
/// left alone, so the default path is unchanged. Returns the number of
/// devices given the queue disc.
uint32_t InstallAntPriority(const NodeContainer& nodes);

}  // namespace ns3

#endif  // ARA_HELPER_H
