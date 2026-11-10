// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "ara-helper.h"

namespace ns3 {

AraHelper::AraHelper() : SwarmRoutingHelper("ns3::ara::RoutingProtocol") {}

AraHelper* AraHelper::Copy() const {
    return new AraHelper(*this);
}

}  // namespace ns3
