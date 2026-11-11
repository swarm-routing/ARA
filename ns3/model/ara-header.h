// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// ARA's control packet in ns-3: the forward and backward ant of core/ara-ant.h
// as an ns3::Header. The bytes are core/'s wire format (version byte, little
// endian, kAntWireSize bytes; org rule 3), written and read by core/'s codec
// on swarm-core's bounds-checked helpers, so ns-3 and the standalone core
// share one encoder and one decoder. Written fresh for this module.
//
// A control packet is untrusted input: the adapter reads it with
// DecodeAntPacket, which takes the whole payload and refuses a wrong version,
// an unknown type, a short buffer or trailing bytes. AntHeader::Deserialize
// cannot fail in ns-3's API, so it records whether the bytes decoded instead.
#ifndef ARA_HEADER_H
#define ARA_HEADER_H

#include "ara-ant.h"

#include "ns3/header.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"

#include <optional>

namespace ns3 {
namespace ara {

/// ARA's simulator-free core (core/), from inside ns3::ara.
namespace core = ::ara::core;

class AntHeader : public Header {
public:
    AntHeader() = default;
    explicit AntHeader(const core::AntMessage& ant) : m_ant(ant), m_valid(true) {}

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    /// Reads kAntWireSize bytes, or what is left if fewer; IsValid() says
    /// whether they decoded.
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    const core::AntMessage& Get() const { return m_ant; }
    bool IsValid() const { return m_valid; }

private:
    core::AntMessage m_ant;
    bool m_valid = false;
};

/// A packet holding exactly one ant.
Ptr<Packet> MakeAntPacket(const core::AntMessage& ant);

/// The ant a received control payload carries, or nullopt when the payload
/// is not exactly one valid ant (core::decodeAnt's rules).
std::optional<core::AntMessage> DecodeAntPacket(Ptr<const Packet> p);

}  // namespace ara
}  // namespace ns3

#endif  // ARA_HEADER_H
