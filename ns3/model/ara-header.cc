// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "ara-header.h"

#include <algorithm>
#include <cstdint>
#include <vector>

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
    std::vector<uint8_t> bytes;
    core::encodeAnt(m_ant, bytes);
    start.Write(bytes.data(), static_cast<uint32_t>(bytes.size()));
}

uint32_t AntHeader::Deserialize(Buffer::Iterator start) {
    const uint32_t n = std::min(start.GetRemainingSize(), GetSerializedSize());
    std::vector<uint8_t> bytes(n);
    if (n > 0) start.Read(bytes.data(), n);  // data() may be null when empty
    const auto ant = core::decodeAnt(bytes);
    m_valid = ant.has_value();
    m_ant = ant.value_or(core::AntMessage{});
    return n;
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
    if (!p || p->GetSize() == 0) return std::nullopt;
    std::vector<uint8_t> bytes(p->GetSize());
    p->CopyData(bytes.data(), static_cast<uint32_t>(bytes.size()));
    return core::decodeAnt(bytes);
}

NS_OBJECT_ENSURE_REGISTERED(DataFlagTag);

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
