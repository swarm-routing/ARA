// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "ara-header.h"

#include "ns3/queue-item.h"
#include "ns3/swarm-wire-buffer.h"

#include <array>
#include <cstdint>

namespace ns3 {
namespace ara {

NS_OBJECT_ENSURE_REGISTERED(AntHeader);

TypeId AntHeader::GetTypeId() {
    static TypeId tid = TypeId("ns3::ara::AntHeader")
                            .SetParent<Header>()
                            .SetGroupName("Ara")
                            .AddConstructor<AntHeader>();
    return tid;
}

TypeId AntHeader::GetInstanceTypeId() const {
    return GetTypeId();
}

uint32_t AntHeader::GetSerializedSize() const {
    return static_cast<uint32_t>(core::kAntWireSize);
}

void AntHeader::Serialize(Buffer::Iterator start) const {
    // #99: straight into the packet's buffer, no vector.
    BufferWireWriter w(start);
    core::writeAnt(m_ant, w);
}

uint32_t AntHeader::Deserialize(Buffer::Iterator start) {
    // #99: into a stack buffer; a short read decodes as invalid.
    std::array<uint8_t, core::kAntWireSize> scratch{};
    const auto bytes = ReadWireBytes(start, scratch, GetSerializedSize());
    const auto ant = core::decodeAnt(bytes);
    m_valid = ant.has_value();
    m_ant = ant.value_or(core::AntMessage{});
    return static_cast<uint32_t>(bytes.size());
}

void AntHeader::Print(std::ostream& os) const {
    if (!m_valid) {
        os << "invalid ant";
        return;
    }
    os << (m_ant.type == core::AntType::Forward ? "FANT" : "BANT") << " src=" << m_ant.source
       << " dst=" << m_ant.destination << " seq=" << m_ant.sequence
       << " hops=" << static_cast<uint32_t>(m_ant.hops)
       << " ttl=" << static_cast<uint32_t>(m_ant.ttl);
}

Ptr<Packet> MakeAntPacket(const core::AntMessage& ant) {
    Ptr<Packet> p = Create<Packet>();
    p->AddHeader(AntHeader(ant));
    return p;
}

std::optional<core::AntMessage> DecodeAntPacket(Ptr<const Packet> p) {
    // A payload of any other size is short or has trailing bytes, which
    // decodeAnt refuses: refused here without copying it (#99).
    if (!p || p->GetSize() != core::kAntWireSize) return std::nullopt;
    std::array<uint8_t, core::kAntWireSize> bytes{};
    p->CopyData(bytes.data(), static_cast<uint32_t>(bytes.size()));
    return core::decodeAnt(bytes);
}

NS_OBJECT_ENSURE_REGISTERED(DataFlagTag);
NS_OBJECT_ENSURE_REGISTERED(DataFlagFilter);

TypeId DataFlagFilter::GetTypeId() {
    static TypeId tid = TypeId("ns3::ara::DataFlagFilter")
                            .SetParent<PacketFilter>()
                            .SetGroupName("Ara")
                            .AddConstructor<DataFlagFilter>();
    return tid;
}

bool DataFlagFilter::CheckProtocol(Ptr<QueueDiscItem> item) const {
    return item && item->GetPacket();
}

int32_t DataFlagFilter::DoClassify(Ptr<QueueDiscItem> item) const {
    return ReadDataFlag(item->GetPacket()) == DataFlagTag::None ? PF_NO_MATCH : 0;
}

TypeId DataFlagTag::GetTypeId() {
    static TypeId tid = TypeId("ns3::ara::DataFlagTag")
                            .SetParent<Tag>()
                            .SetGroupName("Ara")
                            .AddConstructor<DataFlagTag>();
    return tid;
}

TypeId DataFlagTag::GetInstanceTypeId() const {
    return GetTypeId();
}

uint32_t DataFlagTag::GetSerializedSize() const {
    return 1;
}

void DataFlagTag::Serialize(TagBuffer i) const {
    i.WriteU8(m_flag);
}

void DataFlagTag::Deserialize(TagBuffer i) {
    const uint8_t f = i.ReadU8();
    m_flag = f == Backtrack ? Backtrack : f == DuplicateError ? DuplicateError : None;
}

void DataFlagTag::Print(std::ostream& os) const {
    os << (m_flag == Backtrack        ? "backtrack"
           : m_flag == DuplicateError ? "duplicate-error"
                                      : "none");
}

DataFlagTag::Flag ReadDataFlag(Ptr<const Packet> p) {
    DataFlagTag tag;
    return p && p->PeekPacketTag(tag) ? tag.Get() : DataFlagTag::None;
}

}  // namespace ara
}  // namespace ns3
