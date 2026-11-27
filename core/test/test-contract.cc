// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// swarm-core's protocol contract suite (core/testbench/swarm-contract-tests.h,
// its README "Protocol contract suite: how a protocol opts in"; #296) on ARA's
// router: one core::Router per testbench node, its ants on the air.
//
//   DeterministicExecution              seed 1 twice, one trace and one
//                                       contents digest; seed 2 another trace
//                                       (the FANT relay jitter draws differ)
//   NullAndUnavailableProvidersAreSafe  the router takes only the observer of
//                                       the optional ports; with none and with
//                                       a no-op one, one trace
//   ZeroTtlAndSelfLoopPacketsAreDropped a FANT with TTL 0, and one back at its
//                                       source, put nothing on the air
//
// The scene: the suite's 6-node line, 10 s; node 0 sends data toward node 5
// at 1 s and node 5 toward node 0 at 4 s, once a second, so discoveries,
// their BANTs and the retries all run. Data is not on the air here: the
// router decides it, the trace holds the ants. A plain ctest executable,
// outside the NamedTest list: the ns-3 suite builds core/ on a swarm-core
// module that need not ship the testbench.
#include "ara-ant.h"
#include "ara-config.h"
#include "ara-router.h"
#include "testbench/swarm-contract-tests.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace {

namespace tb = swarm::core::testbench;
using ara::core::AntDecision;
using ara::core::AntMessage;
using ara::core::AntType;
using ara::core::Config;
using ara::core::Router;
using swarm::core::NodeAddress;
using swarm::core::Time;

/// A sequence no discovery of the scene reaches, so the suite's probe FANTs
/// are never duplicates of a real one.
constexpr std::uint32_t kProbeSequence = 0x7FFF0000u;

/// The testbench clock as the non-const IClock the router takes.
class ClockView : public swarm::core::IClock {
public:
    explicit ClockView(const swarm::core::IClock& clock) : clock_(clock) {}
    Time now() const override { return clock_.now(); }

private:
    const swarm::core::IClock& clock_;
};

/// One node: a router on the testbench ports, its ant decisions on the air.
class AraNode : public tb::INodeAdapter<AntMessage> {
public:
    AraNode(tb::NodePorts<AntMessage>& ports, const tb::ContractProviders& providers,
            NodeAddress last)
        : ports_(ports), clock_(ports.clock()),
          router_(ports.address(), Config{}, clock_, ports.rng(), ports.timers(),
                  [this](const AntDecision& d) { send(d); }),
          last_(last) {
        router_.setObserver(providers.observer);
    }

    void start() override {
        if (ports_.address() == 0) sendData(last_, 1.0);
        if (ports_.address() == last_) sendData(0, 4.0);
    }
    void receive(NodeAddress from, const AntMessage& msg) override { router_.onAnt(msg, from); }

private:
    /// Data toward `dest` from `at` on, once a second.
    void sendData(NodeAddress dest, Time at) {
        ports_.timers().schedule(at, [this, dest] {
            (void)router_.onLocalData(dest);
            sendData(dest, 1.0);
        });
    }
    void send(const AntDecision& d) {
        if (!d.message) return;
        if (d.action == swarm::core::RouteAction::Broadcast) {
            ports_.broadcast(*d.message);
        } else if (d.action == swarm::core::RouteAction::Unicast) {
            ports_.unicast(d.nextHop, *d.message);
        }
    }

    tb::NodePorts<AntMessage>& ports_;
    ClockView clock_;
    Router router_;
    NodeAddress last_;
};

}  // namespace

int main() {
    tb::ContractSpec<AntMessage> spec;
    const auto last = static_cast<NodeAddress>(spec.topology.size() - 1);
    spec.factory = [last](tb::NodePorts<AntMessage>& p, const tb::ContractProviders& prov) {
        return std::make_unique<AraNode>(p, prov, last);
    };
    spec.kindOf = [](const AntMessage& m) { return static_cast<std::uint8_t>(m.type); };
    spec.payloadHash = [](const AntMessage& m) {
        std::vector<std::uint8_t> bytes;
        ara::core::encodeAnt(m, bytes);
        std::uint64_t h = 0xCBF29CE484222325ull;
        for (std::uint8_t b : bytes) {
            h ^= b;
            h *= 0x100000001B3ull;
        }
        return h;
    };
    spec.hopLimited = [](NodeAddress origin, NodeAddress dest, int hopLimit) {
        const auto ttl = static_cast<std::uint8_t>(hopLimit);
        return AntMessage{AntType::Forward, origin, dest, kProbeSequence, 0, ttl};
    };
    return tb::runProtocolContract(spec) == 0 ? 0 : 1;
}
