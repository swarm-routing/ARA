// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// ARA's control packets: the forward ant (FANT) and the backward ant (BANT)
// of Güneş, Sorges & Bouazizi 2002 ([G02] §3.1), and their wire format.
// Written fresh from the paper and docs/fidelity.md (golden rule 12).
//
// [G02] §3.5: a control packet carries a sequence number and what the IP
// header already holds (source, destination). The hop count is added because
// the initial pheromone depends on it ([G02] §3.1, fidelity "Initial FANT/BANT
// pheromone f(hops)"), and the TTL because a flood needs a bound (fidelity
// "FANT maximum hops / TTL"). The layout is a repo choice under org rule 3:
//
//   byte  0      wire version (kAntWireVersion)
//   byte  1      type (1 = FANT, 2 = BANT)
//   bytes 2-5    source       i32 LE  (the node that created this ant)
//   bytes 6-9    destination  i32 LE
//   bytes 10-13  sequence     u32 LE  (a BANT repeats its FANT's number)
//   byte  14     hops         u8      (hops travelled before this one)
//   byte  15     ttl          u8      (hops it may still travel)
//
// Changing a field or its units bumps kAntWireVersion.
#ifndef ARA_CORE_ANT_H
#define ARA_CORE_ANT_H

#include "swarm-types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ara::core {

using swarm::core::NodeAddress;

/// Bumped whenever a field, its order or its units change (org rule 3).
inline constexpr std::uint8_t kAntWireVersion = 1;
/// Encoded size of every ant: the format has no variable-length part.
inline constexpr std::size_t kAntWireSize = 16;

enum class AntType : std::uint8_t {
    Forward = 1,   ///< FANT: flooded from the source toward the destination.
    Backward = 2,  ///< BANT: unicast back from the destination to the source.
};

struct AntMessage {
    AntType type = AntType::Forward;
    NodeAddress source = swarm::core::kInvalidAddress;       ///< creator of this ant
    NodeAddress destination = swarm::core::kInvalidAddress;  ///< where it is going
    std::uint32_t sequence = 0;
    std::uint8_t hops = 0;
    std::uint8_t ttl = 0;

    bool operator==(const AntMessage&) const = default;
};

/// Appends the ant's kAntWireSize bytes to `out`.
void encodeAnt(const AntMessage& ant, std::vector<std::uint8_t>& out);

/// Decodes exactly one ant from untrusted bytes: nullopt on a wrong version,
/// an unknown type, a short buffer or trailing bytes.
std::optional<AntMessage> decodeAnt(std::span<const std::uint8_t> bytes);

}  // namespace ara::core

#endif  // ARA_CORE_ANT_H
