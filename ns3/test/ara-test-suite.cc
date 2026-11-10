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
//   seed another (org rule 5).
// Through the include path (core/, resolved past the contrib link), not
// "../../core/...": gcov would collapse that beside the link, not its target.
#include "test/ara-core-tests.h"
#include "../model/ara-header.h"

#include "ns3/ara-helper.h"
#include "ns3/ara-routing-protocol.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/swarm-core-module.h"
#include "ns3/test.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/wifi-helper.h"
#include "ns3/yans-wifi-helper.h"

#include <cstdint>
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

// --- the adapter on a chain ------------------------------------------------------

constexpr uint16_t kDataPort = 9;

/// What one chain run observed.
struct ChainRun {
    std::vector<std::string> trace;  ///< ant sends and data receptions, in order
    uint32_t sent = 0;
    uint32_t received = 0;
    uint64_t fants = 0;
    uint64_t bants = 0;
    Ipv4Address nextHop0;  ///< node 0's next hop to node 3 after the run
    Ipv4Address nextHop1;  ///< node 1's
    Ipv4Address expect0;   ///< node 1's address
    Ipv4Address expect1;   ///< node 2's address
    DeferredQueueStats queue0;
};

class Chain {
public:
    ChainRun Run(uint32_t seed, uint32_t packets) {
        SwarmCoreSeed(seed);
        NodeContainer nodes;
        nodes.Create(4);
        MobilityHelper mobility;
        Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
        for (uint32_t i = 0; i < 4; ++i)
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
        channel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange",
                                   DoubleValue(250.0));
        YansWifiPhyHelper phy;
        phy.SetChannel(channel.Create());
        WifiMacHelper mac;
        mac.SetType("ns3::AdhocWifiMac");
        NetDeviceContainer devs = wifi.Install(phy, mac, nodes);

        AraHelper ara;
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
        Ptr<Socket> sink = Socket::CreateSocket(nodes.Get(3), UdpSocketFactory::GetTypeId());
        sink->Bind(InetSocketAddress(Ipv4Address::GetAny(), kDataPort));
        sink->SetRecvCallback(MakeCallback(&Chain::Receive, this));
        m_src = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
        m_dst = ifs.GetAddress(3);
        for (uint32_t k = 0; k < packets; ++k) {
            Simulator::Schedule(Seconds(1.0 + 0.5 * k), &Chain::Send, this);
        }
        Simulator::Stop(Seconds(2.0 + 0.5 * packets));
        Simulator::Run();
        m_run.nextHop0 = Proto(nodes.Get(0))->NextHop(m_dst);
        m_run.nextHop1 = Proto(nodes.Get(1))->NextHop(m_dst);
        m_run.expect0 = ifs.GetAddress(1);
        m_run.expect1 = ifs.GetAddress(2);
        m_run.queue0 = Proto(nodes.Get(0))->GetDeferredQueue()->Stats();
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

class AraTestSuite : public TestSuite {
public:
    AraTestSuite() : TestSuite("ara", Type::UNIT) {
        for (const auto& t : ::ara::core::test::AllTests()) {
            AddTestCase(new AraCoreTestCase(t), TestCase::Duration::QUICK);
        }
        AddTestCase(new HeaderRoundtripTestCase, TestCase::Duration::QUICK);
        AddTestCase(new HeaderFuzzTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ChainDeliveryTestCase, TestCase::Duration::QUICK);
        AddTestCase(new MaintenanceTestCase, TestCase::Duration::QUICK);
        AddTestCase(new DeterminismTestCase, TestCase::Duration::QUICK);
    }
};

AraTestSuite g_araTestSuite;

}  // namespace
