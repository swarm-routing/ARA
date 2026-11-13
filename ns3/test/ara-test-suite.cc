// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// `test.py -s ara`. Needs ns-3.42+ (scoped TestCase::Duration).
//
// - core/'s tests (core/test/), one TestCase each, so the debug and sanitized
//   legs run them inside ns-3 as core.yml runs them alone;
// - the ns-3 header codec: roundtrip through a Packet, and a seeded fuzz of
//   the decoder a received control payload goes through (org rule 3);
// - the adapter on a static four-node 802.11b chain: a FANT flood, the BANT
//   back, data delivered over the trail, the packet sent before the trail
//   dropped rather than held (fidelity "Data packets waiting for a route"),
//   and data keeping that trail alive past its expiry ([G02] §3.2);
// - determinism: the same seed gives the same ant and delivery trace, another
//   seed another (org rule 5);
// - the router observer: the same seed gives a byte-identical run with its
//   trace sources connected, and they agree with the counters (ARA#49);
// - the adapter's guards: no core before the protocol starts, broadcast and
//   multicast data not routed, a malformed control payload ignored (#41).
// Through the include path (core/, resolved past the contrib link), not
// "../../core/...": gcov would collapse that beside the link, not its target.
#include "test/ara-core-tests.h"
#include "../model/ara-header.h"

#include "ns3/ara-helper.h"
#include "ns3/ara-routing-protocol.h"
#include "ns3/boolean.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4.h"
#include "ns3/mobility-helper.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/swarm-core-module.h"
#include "ns3/test.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/wifi-helper.h"
#include "ns3/yans-wifi-helper.h"

#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;
using ::ara::core::AntMessage;
using ::ara::core::AntType;

namespace {

/// One core/ test function, its failures reported through ns-3.
class AraCoreTestCase : public TestCase {
public:
    explicit AraCoreTestCase(const ::ara::core::test::NamedTest& t)
        : TestCase(std::string("core: ") + t.name), m_test(t) {}

private:
    void DoRun() override {
        ::ara::core::test::Checker c;
        m_test.run(c);
        // Each failure prints as "got <file:line: check>, expected ''".
        for (const auto& f : c.failures()) {
            NS_TEST_EXPECT_MSG_EQ(f, std::string(), "core check failed");
        }
    }

    ::ara::core::test::NamedTest m_test;
};

std::vector<uint8_t> Bytes(Ptr<const Packet> p) {
    std::vector<uint8_t> b(p->GetSize());
    p->CopyData(b.data(), static_cast<uint32_t>(b.size()));
    return b;
}

Ptr<Packet> FromBytes(const std::vector<uint8_t>& b) {
    if (b.empty()) return Create<Packet>();  // data() may be null
    return Create<Packet>(b.data(), static_cast<uint32_t>(b.size()));
}

class HeaderRoundtripTestCase : public TestCase {
public:
    HeaderRoundtripTestCase() : TestCase("header: AntHeader roundtrip through a Packet") {}

private:
    void DoRun() override {
        const std::vector<AntMessage> ants{
            {AntType::Forward, 0x0A010001, 0x0A010004, 1, 0, 35},
            {AntType::Backward, 0x0A010004, 0x0A010001, 0xFFFFFFFFu, 255, 1},
            {AntType::Forward, -2, 0x7FFFFFFF, 0x80000000u, 17, 0},
        };
        for (const AntMessage& a : ants) {
            Ptr<Packet> p = ns3::ara::MakeAntPacket(a);
            NS_TEST_ASSERT_MSG_EQ(p->GetSize(), ::ara::core::kAntWireSize, "one fixed-size ant");
            NS_TEST_EXPECT_MSG_EQ(Bytes(p)[0], ::ara::core::kAntWireVersion,
                                  "leading version byte");
            const auto decoded = ns3::ara::DecodeAntPacket(p);
            NS_TEST_ASSERT_MSG_EQ(decoded.has_value(), true, "decodes");
            NS_TEST_EXPECT_MSG_EQ((*decoded == a), true, "same ant back");
            ns3::ara::AntHeader h;
            NS_TEST_EXPECT_MSG_EQ(p->Copy()->RemoveHeader(h), ::ara::core::kAntWireSize,
                                  "RemoveHeader consumes the ant");
            NS_TEST_EXPECT_MSG_EQ(h.IsValid(), true, "header valid");
            NS_TEST_EXPECT_MSG_EQ((h.Get() == a), true, "same ant through the header");
        }
        // The core's byte layout, little endian (core/ara-ant.h).
        const auto b = Bytes(ns3::ara::MakeAntPacket(ants[0]));
        const std::vector<uint8_t> want{1,    1,    0x01, 0x00, 0x01, 0x0A, 0x04, 0x00,
                                        0x01, 0x0A, 1,    0,    0,    0,    0,    35};
        NS_TEST_EXPECT_MSG_EQ((b == want), true, "wire layout");
        // A wrong version through the header: decoded as invalid, not trusted.
        std::vector<uint8_t> bad = want;
        bad[0] = ::ara::core::kAntWireVersion + 1;
        ns3::ara::AntHeader h;
        FromBytes(bad)->RemoveHeader(h);
        NS_TEST_EXPECT_MSG_EQ(h.IsValid(), false, "a wrong version is invalid");
    }
};

/// SplitMix64, as core/'s tests: the same bytes on every platform.
uint64_t Mix(uint64_t& s) {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

class HeaderFuzzTestCase : public TestCase {
public:
    HeaderFuzzTestCase() : TestCase("header: seeded fuzz of the control-payload decoder") {}

private:
    void DoRun() override {
        uint64_t s = 0xA4A5;
        std::vector<uint8_t> valid;
        ::ara::core::encodeAnt({AntType::Backward, 7, 9, 42, 3, 30}, valid);
        uint32_t accepted = 0;
        for (int i = 0; i < 4000; ++i) {
            std::vector<uint8_t> b;
            if (i % 2 == 0) {
                // Random bytes, 0 to 24 long.
                b.resize(Mix(s) % 25);
                for (auto& x : b)
                    x = static_cast<uint8_t>(Mix(s));
                if (!b.empty() && i % 4 == 0) b[0] = ::ara::core::kAntWireVersion;
            } else {
                // A valid ant with one byte changed, cut short or extended.
                b = valid;
                b[Mix(s) % b.size()] = static_cast<uint8_t>(Mix(s));
                const uint64_t shape = Mix(s) % 3;
                if (shape == 1) b.resize(Mix(s) % b.size());
                if (shape == 2) b.push_back(static_cast<uint8_t>(Mix(s)));
            }
            const auto ant = ns3::ara::DecodeAntPacket(FromBytes(b));
            if (!ant) continue;
            ++accepted;
            // Whatever is accepted is exactly one canonical ant.
            NS_TEST_ASSERT_MSG_EQ(b.size(), ::ara::core::kAntWireSize, "accepted size");
            std::vector<uint8_t> again;
            ::ara::core::encodeAnt(*ant, again);
            NS_TEST_ASSERT_MSG_EQ((again == b), true, "accepted bytes re-encode identically");
        }
        NS_TEST_EXPECT_MSG_GT(accepted, 0u, "the fuzz reaches the accepting path");
        NS_TEST_EXPECT_MSG_EQ(ns3::ara::DecodeAntPacket(Create<Packet>()).has_value(), false,
                              "empty payload");
        std::vector<uint8_t> trailing = valid;
        trailing.push_back(0);
        NS_TEST_EXPECT_MSG_EQ(ns3::ara::DecodeAntPacket(FromBytes(trailing)).has_value(), false,
                              "trailing byte");
        std::vector<uint8_t> type = valid;
        type[1] = 3;
        NS_TEST_EXPECT_MSG_EQ(ns3::ara::DecodeAntPacket(FromBytes(type)).has_value(), false,
                              "unknown type");
    }
};

class HeaderPrintTestCase : public TestCase {
public:
    HeaderPrintTestCase() : TestCase("header: Print names the ant, or says it is invalid") {}

private:
    void DoRun() override {
        std::ostringstream fant;
        ns3::ara::AntHeader(AntMessage{AntType::Forward, 1, 4, 7, 2, 33}).Print(fant);
        NS_TEST_EXPECT_MSG_EQ(fant.str(), std::string("FANT src=1 dst=4 seq=7 hops=2 ttl=33"),
                              "a FANT");
        std::ostringstream bant;
        ns3::ara::AntHeader(AntMessage{AntType::Backward, 4, 1, 7, 0, 35}).Print(bant);
        NS_TEST_EXPECT_MSG_EQ(bant.str(), std::string("BANT src=4 dst=1 seq=7 hops=0 ttl=35"),
                              "a BANT");
        // Bytes that do not decode (a short buffer) print as invalid.
        ns3::ara::AntHeader h;
        Create<Packet>(3)->RemoveHeader(h);
        std::ostringstream bad;
        h.Print(bad);
        NS_TEST_EXPECT_MSG_EQ(bad.str(), std::string("invalid ant"), "undecodable bytes");
    }
};

// --- the adapter on a chain ------------------------------------------------------

constexpr uint16_t kDataPort = 9;

/// Places `nodes` on a line 200 m apart, each reaching only its neighbours
/// (250 m range), on 802.11b at 2 Mbit/s.
NetDeviceContainer LineOfWifiNodes(NodeContainer& nodes) {
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
        pos->Add(Vector(200.0 * i, 0.0, 0.0));
    mobility.SetPositionAllocator(pos);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", "DataMode",
                                 StringValue("DsssRate2Mbps"), "ControlMode",
                                 StringValue("DsssRate1Mbps"));
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(250.0));
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    return wifi.Install(phy, mac, nodes);
}

/// What one chain run observed.
struct ChainRun {
    std::vector<std::string> trace;  ///< ant sends and data receptions, in order
    uint32_t sent = 0;
    uint32_t received = 0;
    uint64_t fants = 0;
    uint64_t bants = 0;
    Ipv4Address nextHop0;  ///< node 0's next hop to node 3 after the run
    Ipv4Address nextHop1;  ///< node 1's
    Ipv4Address nextHop2;  ///< node 2's
    Ipv4Address expect0;   ///< node 1's address
    Ipv4Address expect1;   ///< node 2's address
    DeferredQueueStats queue0;
    // The router observer: the trace sources' own tallies, and the counters.
    uint64_t ctlTx = 0;
    uint64_t ctlTxBytes = 0;
    uint64_t ctlRx = 0;
    uint64_t routeAdds = 0;
    uint64_t routeRemoves = 0;
    std::map<int, uint64_t> decisions;  ///< by RouteAction
    uint64_t stateReports = 0;
    uint32_t lastEntries = 0;
    swarm::core::RouterStats stats;  ///< SwarmRoutingHelper::CollectStats' aggregate
    std::string fantName;
    std::string bantName;
};

class Chain {
public:
    /// `observe` connects the router observer's trace sources, which must not
    /// change the run (org rule 5). `buffer` sets BufferDuringDiscovery;
    /// `extra` runs that much longer after the last packet.
    ChainRun Run(uint32_t seed, uint32_t packets, bool observe = false, double moveAt = 0.0,
                 bool buffer = false, double extra = 0.0) {
        SwarmCoreSeed(seed);
        NodeContainer nodes;
        nodes.Create(4);
        NetDeviceContainer devs = LineOfWifiNodes(nodes);

        AraHelper ara;
        if (buffer) {
            ara.Set("BufferDuringDiscovery", BooleanValue(true));
        }
        ara.Install(nodes);
        Ipv4AddressHelper a;
        a.SetBase("10.1.0.0", "255.255.255.0");
        Ipv4InterfaceContainer ifs = a.Assign(devs);
        // Every stream pinned, so a second run in this process draws as the first.
        int64_t stream = ara.AssignStreams(nodes, 0);
        stream += WifiHelper::AssignStreams(devs, stream);
        InternetStackHelper().AssignStreams(nodes, stream);

        m_run = ChainRun{};
        for (uint32_t i = 0; i < 4; ++i) {
            Proto(nodes.Get(i))
                ->TraceConnect("AntTx", std::to_string(i), MakeCallback(&Chain::AntTx, this));
        }
        if (observe) {
            for (uint32_t i = 0; i < 4; ++i) {
                Ptr<ns3::ara::RoutingProtocol> p = Proto(nodes.Get(i));
                p->TraceConnectWithoutContext("ControlTx", MakeCallback(&Chain::ControlTx, this));
                p->TraceConnectWithoutContext("ControlRx", MakeCallback(&Chain::ControlRx, this));
                p->TraceConnectWithoutContext("RouteChanged",
                                              MakeCallback(&Chain::RouteChanged, this));
                p->TraceConnectWithoutContext("DataDecision",
                                              MakeCallback(&Chain::DataDecision, this));
                p->TraceConnectWithoutContext("StateSize", MakeCallback(&Chain::StateSize, this));
            }
        }
        Ptr<Socket> sink = Socket::CreateSocket(nodes.Get(3), UdpSocketFactory::GetTypeId());
        sink->Bind(InetSocketAddress(Ipv4Address::GetAny(), kDataPort));
        sink->SetRecvCallback(MakeCallback(&Chain::Receive, this));
        m_src = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
        m_dst = ifs.GetAddress(3);
        for (uint32_t k = 0; k < packets; ++k) {
            Simulator::Schedule(Seconds(1.0 + 0.5 * k), &Chain::Send, this);
        }
        if (moveAt > 0.0) {  // the destination walks out of every node's range
            Ptr<Node> dst = nodes.Get(3);
            Simulator::Schedule(Seconds(moveAt), [dst]() {
                dst->GetObject<MobilityModel>()->SetPosition(Vector(5000.0, 0.0, 0.0));
            });
        }
        Simulator::Stop(Seconds(2.0 + 0.5 * packets + extra));
        Simulator::Run();
        m_run.nextHop0 = Proto(nodes.Get(0))->NextHop(m_dst);
        m_run.nextHop1 = Proto(nodes.Get(1))->NextHop(m_dst);
        m_run.nextHop2 = Proto(nodes.Get(2))->NextHop(m_dst);
        m_run.expect0 = ifs.GetAddress(1);
        m_run.expect1 = ifs.GetAddress(2);
        m_run.queue0 = Proto(nodes.Get(0))->GetDeferredQueue()->Stats();
        m_run.stats = SwarmRoutingHelper::CollectStats(nodes).aggregate;
        m_run.fantName = ara.KindName(static_cast<uint8_t>(AntType::Forward));
        m_run.bantName = ara.KindName(static_cast<uint8_t>(AntType::Backward));
        m_src = nullptr;
        Simulator::Destroy();
        return m_run;
    }

private:
    static Ptr<ns3::ara::RoutingProtocol> Proto(Ptr<Node> n) {
        return n->GetObject<ns3::ara::RoutingProtocol>();
    }
    void AntTx(std::string node, uint8_t type, bool broadcast) {
        (type == static_cast<uint8_t>(AntType::Forward) ? m_run.fants : m_run.bants)++;
        std::ostringstream os;
        os << Simulator::Now().GetNanoSeconds() << " n" << node << " tx type=" << int{type}
           << (broadcast ? " bcast" : " ucast");
        m_run.trace.push_back(os.str());
    }
    void ControlTx(uint8_t, bool, uint32_t bytes) {
        ++m_run.ctlTx;
        m_run.ctlTxBytes += bytes;
    }
    void ControlRx(uint8_t, uint32_t) { ++m_run.ctlRx; }
    void RouteChanged(Ipv4Address, Ipv4Address, bool added) {
        ++(added ? m_run.routeAdds : m_run.routeRemoves);
    }
    void DataDecision(Ipv4Address, swarm::core::RouteAction a) {
        ++m_run.decisions[static_cast<int>(a)];
    }
    void StateSize(uint32_t entries, uint32_t) {
        ++m_run.stateReports;
        m_run.lastEntries = entries;
    }
    void Send() {
        ++m_run.sent;
        m_src->SendTo(Create<Packet>(64), 0, InetSocketAddress(m_dst, kDataPort));
    }
    void Receive(Ptr<Socket> s) {
        while (Ptr<Packet> p = s->Recv()) {
            ++m_run.received;
            std::ostringstream os;
            os << Simulator::Now().GetNanoSeconds() << " n3 rx data";
            m_run.trace.push_back(os.str());
        }
    }

    ChainRun m_run;
    Ptr<Socket> m_src;
    Ipv4Address m_dst;
};

class ChainDeliveryTestCase : public TestCase {
public:
    ChainDeliveryTestCase()
        : TestCase("adapter: discovery and delivery on a 4-node chain, no buffering") {}

private:
    void DoRun() override {
        const ChainRun r = Chain().Run(1, 20);
        NS_TEST_ASSERT_MSG_EQ(r.sent, 20u, "packets sent");
        NS_TEST_EXPECT_MSG_GT(r.fants, 0u, "FANTs flooded");
        NS_TEST_EXPECT_MSG_GT(r.bants, 0u, "BANTs answered");
        // The first packet finds no trail: it starts the discovery and is
        // dropped, not held; the rest follow the trail the BANT laid.
        NS_TEST_EXPECT_MSG_EQ(r.received, 19u, "every packet after the discovery delivered");
        NS_TEST_EXPECT_MSG_EQ(r.queue0.enqueued, 1u, "one packet without a trail");
        NS_TEST_EXPECT_MSG_EQ(r.queue0.discarded, 1u, "and it was dropped at once");
        NS_TEST_EXPECT_MSG_EQ(r.queue0.released, 0u, "nothing held for later");
        NS_TEST_EXPECT_MSG_EQ(r.nextHop0, r.expect0, "node 0 routes to node 3 over node 1");
        NS_TEST_EXPECT_MSG_EQ(r.nextHop1, r.expect1, "node 1 routes to node 3 over node 2");
    }
};

class MaintenanceTestCase : public TestCase {
public:
    MaintenanceTestCase()
        : TestCase("adapter: data keeps the trail alive past its expiry ([G02] §3.2)") {}

private:
    void DoRun() override {
        // 80 packets, one every 0.5 s from t = 1 s to 40.5 s. The discovery's
        // trail at node 0 is 1/3 (three hops), which unvisited falls below the
        // 0.01 expiry about 33 s later; each packet adds Δϕ on every hop.
        const ChainRun r = Chain().Run(1, 80);
        NS_TEST_ASSERT_MSG_EQ(r.sent, 80u, "packets sent");
        NS_TEST_EXPECT_MSG_EQ(r.received, 79u, "every packet after the discovery delivered");
        NS_TEST_EXPECT_MSG_EQ(r.queue0.enqueued, 1u, "only the first packet found no trail");
        NS_TEST_EXPECT_MSG_EQ(r.nextHop0, r.expect0, "node 0 still routes over node 1");
        NS_TEST_EXPECT_MSG_EQ(r.nextHop1, r.expect1, "node 1 still routes over node 2");
    }
};

/// The BufferDuringDiscovery switch (#5): the packet that starts the
/// discovery is held and sent when the BANT lays the trail; with no answer,
/// the held packets go when the discovery ends. Off, ChainDeliveryTestCase.
class BufferTestCase : public TestCase {
public:
    BufferTestCase()
        : TestCase("adapter: BufferDuringDiscovery holds data while the discovery runs (#5)") {}

private:
    void DoRun() override {
        const ChainRun r = Chain().Run(1, 20, false, 0.0, true);
        NS_TEST_EXPECT_MSG_EQ(r.received, 20u, "every packet delivered, the first one too");
        NS_TEST_EXPECT_MSG_EQ(r.queue0.enqueued, 1u, "the first packet found no trail");
        NS_TEST_EXPECT_MSG_EQ(r.queue0.released, 1u, "held, then sent when the BANT came back");
        NS_TEST_EXPECT_MSG_EQ(r.queue0.discarded, 0u, "nothing dropped");
        NS_TEST_EXPECT_MSG_EQ(r.nextHop0, r.expect0, "node 0 routes to node 3 over node 1");

        // The destination is gone before the first packet: no BANT. One
        // packet, so no retry: the discovery ends at 1 + 2.8 s with no trail.
        const ChainRun g = Chain().Run(1, 1, false, 0.5, true, 5.0);
        NS_TEST_EXPECT_MSG_EQ(g.received, 0u, "nothing to deliver to");
        NS_TEST_EXPECT_MSG_EQ(g.queue0.enqueued, 1u, "held while the discovery ran");
        NS_TEST_EXPECT_MSG_EQ(g.queue0.discarded, 1u, "dropped when it ended with no trail");
        NS_TEST_EXPECT_MSG_EQ(g.queue0.released, 0u, "never sent");
    }
};

/// [G02] §3.3 end to end: the destination walks away at 5.25 s; the next
/// packet's MAC failure at node 2 backtracks it through node 1 to the source,
/// each hop dropping its dead trail, and the source starts a new discovery.
class BacktrackTestCase : public TestCase {
public:
    BacktrackTestCase()
        : TestCase("adapter: a failed link backtracks the packet to the source ([G02] §3.3)") {}

private:
    void DoRun() override {
        const ChainRun r = Chain().Run(1, 12, false, 5.25);
        NS_TEST_EXPECT_MSG_EQ(r.received, 8u, "the packets before the move delivered");
        NS_TEST_EXPECT_MSG_EQ(r.nextHop2, Ipv4Address(), "node 2 dropped its trail over node 3");
        NS_TEST_EXPECT_MSG_EQ(r.nextHop1, Ipv4Address(), "node 1 dropped its trail over node 2");
        NS_TEST_EXPECT_MSG_EQ(r.nextHop0, Ipv4Address(), "the source dropped its trail");
        uint32_t fantsAfter = 0;
        for (const std::string& line : r.trace) {
            const double t = std::stod(line.substr(0, line.find(' '))) * 1e-9;
            fantsAfter += t > 5.25 && line.find(" n0 tx type=1 bcast") != std::string::npos;
        }
        NS_TEST_EXPECT_MSG_GT(fantsAfter, 0u, "the source started a new discovery");
    }
};

class DeterminismTestCase : public TestCase {
public:
    DeterminismTestCase() : TestCase("determinism: same seed same trace, another seed another") {}

private:
    void DoRun() override {
        const ChainRun a = Chain().Run(1, 10);
        const ChainRun b = Chain().Run(1, 10);
        const ChainRun c = Chain().Run(2, 10);
        NS_TEST_ASSERT_MSG_GT(a.trace.size(), 0u, "something happened");
        NS_TEST_EXPECT_MSG_EQ((a.trace == b.trace), true, "seed 1 twice: identical");
        // The relay jitter is drawn from the seeded stream, so the FANT
        // relays leave at other times under another seed.
        NS_TEST_EXPECT_MSG_EQ((a.trace != c.trace), true, "seed 2: a different trace");
    }
};

class ObserverTestCase : public TestCase {
public:
    ObserverTestCase()
        : TestCase("observer: trace sources and counters, same run with and without them") {}

private:
    void DoRun() override {
        const ChainRun plain = Chain().Run(1, 20);
        ChainRun seen = Chain().Run(1, 20, true);
        NS_TEST_ASSERT_MSG_GT(plain.trace.size(), 0u, "something happened");
        NS_TEST_EXPECT_MSG_EQ((seen.trace == plain.trace), true,
                              "same seed: byte-identical with the trace sources connected");
        NS_TEST_EXPECT_MSG_EQ(seen.received, plain.received, "same deliveries");

        // The trace sources agree with the ants that left (AntTx) and the counters.
        NS_TEST_EXPECT_MSG_EQ(seen.ctlTx, seen.fants + seen.bants, "ControlTx per ant sent");
        NS_TEST_EXPECT_MSG_EQ(seen.ctlTxBytes, seen.ctlTx * ::ara::core::kAntWireSize,
                              "each ant is its encoded size");
        NS_TEST_EXPECT_MSG_GT(seen.ctlRx, 0u, "ants received");
        NS_TEST_EXPECT_MSG_GT(seen.routeAdds, 0u, "trails laid");
        NS_TEST_EXPECT_MSG_GT(seen.stateReports, 0u, "the table's size reported");
        // Node 0 originates 20 packets: 1 dropped (no trail), 19 forwarded; the relays
        // forward 19 each, and node 3 delivers 19.
        NS_TEST_EXPECT_MSG_EQ(seen.decisions[static_cast<int>(swarm::core::RouteAction::Drop)], 1u,
                              "one drop: the packet without a trail");
        NS_TEST_EXPECT_MSG_EQ(seen.decisions[static_cast<int>(swarm::core::RouteAction::Deliver)],
                              19u, "19 delivered");
        NS_TEST_EXPECT_MSG_EQ(seen.decisions[static_cast<int>(swarm::core::RouteAction::Unicast)],
                              3u * 19u, "19 forwarded at each of nodes 0, 1 and 2");

        // The counters are kept with or without trace sources connected.
        NS_TEST_EXPECT_MSG_EQ((seen.stats == plain.stats), true,
                              "counters do not depend on traces");
        const auto fant = seen.stats.control.find(static_cast<uint8_t>(AntType::Forward));
        const auto bant = seen.stats.control.find(static_cast<uint8_t>(AntType::Backward));
        NS_TEST_ASSERT_MSG_EQ(
            (fant != seen.stats.control.end() && bant != seen.stats.control.end()), true,
            "both kinds counted");
        NS_TEST_EXPECT_MSG_EQ(fant->second.txPackets, seen.fants, "FANTs sent");
        NS_TEST_EXPECT_MSG_EQ(bant->second.txPackets, seen.bants, "BANTs sent");
        NS_TEST_EXPECT_MSG_EQ(seen.stats.routeAdds, seen.routeAdds, "route adds");
        NS_TEST_EXPECT_MSG_EQ(seen.stats.routeRemoves, seen.routeRemoves, "route removes");
        NS_TEST_EXPECT_MSG_GT(seen.stats.peakStateBytes, 0u, "a state footprint");
        NS_TEST_EXPECT_MSG_EQ(seen.fantName, "FANT", "kind names are registered");
        NS_TEST_EXPECT_MSG_EQ(seen.bantName, "BANT", "kind names are registered");
    }
};

// --- the adapter's guards, through its hooks --------------------------------------

/// ns3::ara::RoutingProtocol with its protected hooks callable from a test.
class ProbeProtocol : public ns3::ara::RoutingProtocol {
public:
    static TypeId GetTypeId();
    /// SelectNextHop for data to `dst` that arrived from `prev`.
    Ipv4Address Select(Ipv4Address dst, Ipv4Address prev) {
        return SelectNextHop(dst, Ipv4Address(), prev, nullptr);
    }
    /// RecvControl of a payload from the one-hop neighbour `from`.
    void Control(Ptr<Packet> p, Ipv4Address from) { RecvControl(p, from, Ipv4Address()); }
};

NS_OBJECT_ENSURE_REGISTERED(ProbeProtocol);

TypeId ProbeProtocol::GetTypeId() {
    static TypeId tid = TypeId("ns3::aratest::ProbeProtocol")
                            .SetParent<ns3::ara::RoutingProtocol>()
                            .SetGroupName("Ara")
                            .AddConstructor<ProbeProtocol>();
    return tid;
}

/// With BufferDuringDiscovery on, a node discovering a destination holds its
/// own data for it, and still drops at once a transit packet for the same
/// destination it has no trail for, as the core decides (#112 review): the
/// held packets stay.
class BufferTransitTestCase : public TestCase {
public:
    BufferTransitTestCase()
        : TestCase("adapter: buffering holds local data only; transit data with no trail drops") {}

private:
    void Forward(Ptr<Ipv4Route>, Ptr<const Packet>, const Ipv4Header&) { ++m_forwarded; }
    void Error(Ptr<const Packet> p, const Ipv4Header&, Socket::SocketErrno) {
        m_errors.push_back(p->GetUid());
    }

    void DoRun() override {
        SwarmCoreSeed(1);
        NodeContainer nodes;
        nodes.Create(2);
        NetDeviceContainer devs = LineOfWifiNodes(nodes);
        SwarmRoutingHelper probe("ns3::aratest::ProbeProtocol");
        probe.Set("BufferDuringDiscovery", BooleanValue(true));
        probe.Install(nodes);
        Ipv4AddressHelper a;
        a.SetBase("10.1.0.0", "255.255.255.0");
        Ipv4InterfaceContainer ifs = a.Assign(devs);
        probe.AssignStreams(nodes, 0);
        Ptr<ProbeProtocol> p0 = nodes.Get(0)->GetObject<ProbeProtocol>();
        Ptr<Ipv4> ip0 = nodes.Get(0)->GetObject<Ipv4>();
        const Ipv4Address self = ifs.GetAddress(0);
        const Ipv4Address n1 = ifs.GetAddress(1);
        const Ipv4Address far("10.1.0.99");  // nobody: the discovery gets no BANT
        const auto ucb = MakeCallback(&BufferTransitTestCase::Forward, this);
        const auto ecb = MakeCallback(&BufferTransitTestCase::Error, this);

        uint64_t local = 0;
        uint64_t transit = 0;
        Simulator::Schedule(Seconds(0.5), [&]() {
            // This node's own packet: no trail, so the core starts a discovery
            // and answers Queue; it comes back through loopback and is held.
            NS_TEST_EXPECT_MSG_EQ(p0->Select(far, Ipv4Address()), Ipv4Address(), "no trail yet");
            Ipv4Header h;
            h.SetSource(self);
            h.SetDestination(far);
            h.SetProtocol(6);
            h.SetTtl(64);
            Ptr<Packet> mine = Create<Packet>(64);
            local = mine->GetUid();
            p0->RouteInput(mine, h, ip0->GetNetDevice(0), ucb, {}, {}, ecb);
            // A neighbour's packet for the same destination, in transit here.
            Ipv4Header t = h;
            t.SetSource(n1);
            t.SetIdentification(1);
            Ptr<Packet> theirs = Create<Packet>(64);
            theirs->AddPacketTag(SwarmPrevHopTag(n1));
            transit = theirs->GetUid();
            p0->RouteInput(theirs, t, ip0->GetNetDevice(1), ucb, {}, {}, ecb);
        });
        Simulator::Schedule(Seconds(1.0), [&]() {
            Ptr<DeferredPacketQueue> q = p0->GetDeferredQueue();
            NS_TEST_EXPECT_MSG_EQ(m_errors.size(), 1u, "one packet dropped");
            if (!m_errors.empty()) {
                NS_TEST_EXPECT_MSG_EQ(m_errors[0], transit, "the transit one");
            }
            NS_TEST_EXPECT_MSG_EQ(q->Size(), 1u, "this node's own packet is still held");
            NS_TEST_EXPECT_MSG_EQ(q->Has(far), true, "for the destination it discovers");
            NS_TEST_EXPECT_MSG_EQ(m_forwarded, 0u, "nothing forwarded");
        });
        // The discovery ends with no trail at 0.5 + 2.8 s: the held packet goes.
        Simulator::Schedule(Seconds(4.0), [&]() {
            NS_TEST_EXPECT_MSG_EQ(m_errors.size(), 2u, "then the held packet is dropped");
            if (m_errors.size() == 2) {
                NS_TEST_EXPECT_MSG_EQ(m_errors[1], local, "this node's own");
            }
            NS_TEST_EXPECT_MSG_EQ(p0->GetDeferredQueue()->Size(), 0u, "nothing held");
        });
        Simulator::Stop(Seconds(4.5));
        Simulator::Run();
        Simulator::Destroy();
    }

    uint32_t m_forwarded = 0;
    std::vector<uint64_t> m_errors;
};

class AdapterGuardsTestCase : public TestCase {
public:
    AdapterGuardsTestCase()
        : TestCase("adapter: idle protocol, broadcast and multicast data, malformed ants") {}

private:
    void DoRun() override {
        const Ipv4Address far("10.1.0.99");
        std::vector<uint8_t> fant;
        ::ara::core::encodeAnt({AntType::Forward, 0x0A010002, 0x0A010063, 7, 0, 35}, fant);

        // Before the protocol starts there is no core: no route, no discovery,
        // and a control payload is ignored.
        Ptr<ProbeProtocol> idle = CreateObject<ProbeProtocol>();
        NS_TEST_EXPECT_MSG_EQ(idle->NextHop(far), Ipv4Address(), "idle: no next hop");
        NS_TEST_EXPECT_MSG_EQ(idle->Select(far, Ipv4Address()), Ipv4Address(),
                              "idle: nothing selected");
        idle->Control(FromBytes(fant), Ipv4Address("10.1.0.2"));

        // Two neighbours, both running the probe (node 0 at 10.1.0.1).
        SwarmCoreSeed(1);
        NodeContainer nodes;
        nodes.Create(2);
        NetDeviceContainer devs = LineOfWifiNodes(nodes);
        SwarmRoutingHelper probe("ns3::aratest::ProbeProtocol");
        probe.Install(nodes);
        Ipv4AddressHelper a;
        a.SetBase("10.1.0.0", "255.255.255.0");
        Ipv4InterfaceContainer ifs = a.Assign(devs);
        probe.AssignStreams(nodes, 0);
        Ptr<ProbeProtocol> p0 = nodes.Get(0)->GetObject<ProbeProtocol>();
        p0->TraceConnectWithoutContext("AntTx", MakeCallback(&AdapterGuardsTestCase::AntTx, this));
        const Ipv4Address n1 = ifs.GetAddress(1);

        Simulator::Schedule(Seconds(0.5), [&]() {
            // Broadcast and multicast data are not routed, and start no discovery.
            NS_TEST_EXPECT_MSG_EQ(p0->Select(Ipv4Address::GetBroadcast(), Ipv4Address()),
                                  Ipv4Address(), "broadcast data");
            NS_TEST_EXPECT_MSG_EQ(p0->Select(Ipv4Address("224.0.0.1"), Ipv4Address()),
                                  Ipv4Address(), "multicast data");
            // A malformed payload changes nothing (a wrong version, then a
            // short one); the valid FANT after it lays a trail and is relayed.
            std::vector<uint8_t> wrong = fant;
            wrong[0] = ::ara::core::kAntWireVersion + 1;
            p0->Control(FromBytes(wrong), n1);
            p0->Control(Create<Packet>(3), n1);
            NS_TEST_EXPECT_MSG_EQ(p0->NextHop(n1), Ipv4Address(), "no trail from a bad ant");
            p0->Control(FromBytes(fant), n1);
            NS_TEST_EXPECT_MSG_EQ(p0->NextHop(n1), n1, "the valid FANT laid its trail");
        });
        Simulator::Stop(Seconds(1.0));
        Simulator::Run();
        Simulator::Destroy();
        // Only the valid FANT's relay was sent: no discovery for the broadcast
        // or multicast packet, nothing for the malformed ants.
        NS_TEST_EXPECT_MSG_EQ(m_tx.size(), 1u, "one ant sent by node 0");
        if (!m_tx.empty()) {
            NS_TEST_EXPECT_MSG_EQ(m_tx[0], std::string("type=1 bcast"), "the FANT relay");
        }
    }

    void AntTx(uint8_t type, bool broadcast) {
        m_tx.push_back("type=" + std::to_string(type) + (broadcast ? " bcast" : " ucast"));
    }

    std::vector<std::string> m_tx;
};

class AraTestSuite : public TestSuite {
public:
    AraTestSuite() : TestSuite("ara", Type::UNIT) {
        for (const auto& t : ::ara::core::test::AllTests()) {
            AddTestCase(new AraCoreTestCase(t), TestCase::Duration::QUICK);
        }
        AddTestCase(new HeaderRoundtripTestCase, TestCase::Duration::QUICK);
        AddTestCase(new HeaderFuzzTestCase, TestCase::Duration::QUICK);
        AddTestCase(new HeaderPrintTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ChainDeliveryTestCase, TestCase::Duration::QUICK);
        AddTestCase(new MaintenanceTestCase, TestCase::Duration::QUICK);
        AddTestCase(new BacktrackTestCase, TestCase::Duration::QUICK);
        AddTestCase(new BufferTestCase, TestCase::Duration::QUICK);
        AddTestCase(new DeterminismTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ObserverTestCase, TestCase::Duration::QUICK);
        AddTestCase(new BufferTransitTestCase, TestCase::Duration::QUICK);
        AddTestCase(new AdapterGuardsTestCase, TestCase::Duration::QUICK);
    }
};

AraTestSuite g_araTestSuite;

}  // namespace
