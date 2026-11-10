// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The ant codec, on swarm-core's bounds-checked little-endian helpers.
#include "ara-ant.h"

#include "swarm-wire.h"

namespace ara::core {

void encodeAnt(const AntMessage& ant, std::vector<std::uint8_t>& out) {
    swarm::core::WireWriter w(out);
    w.version(kAntWireVersion);
    w.u8(static_cast<std::uint8_t>(ant.type));
    w.i32(ant.source);
    w.i32(ant.destination);
    w.u32(ant.sequence);
    w.u8(ant.hops);
    w.u8(ant.ttl);
}

std::optional<AntMessage> decodeAnt(std::span<const std::uint8_t> bytes) {
    swarm::core::WireReader r(bytes);
    r.expectVersion(kAntWireVersion);
    AntMessage ant;
    const std::uint8_t type = r.u8();
    if (type != static_cast<std::uint8_t>(AntType::Forward) &&
        type != static_cast<std::uint8_t>(AntType::Backward)) {
        r.fail();
    }
    ant.type = static_cast<AntType>(type);
    ant.source = r.i32();
    ant.destination = r.i32();
    ant.sequence = r.u32();
    ant.hops = r.u8();
    ant.ttl = r.u8();
    if (!r.atEnd()) return std::nullopt;
    return ant;
}

}  // namespace ara::core
