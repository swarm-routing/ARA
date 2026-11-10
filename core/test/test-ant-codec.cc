// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The ant wire format (org rule 3): a versioned, fixed-size, little-endian
// POD; deterministic roundtrip and seeded fuzz.
#include "ara-core-tests.h"

#include <cstdint>
#include <vector>

namespace ara::core::test {

void TestAntRoundtrip(Checker& c) {
    const AntMessage fant{AntType::Forward, 0x01020304, -2, 0xA1B2C3D4u, 7, 35};
    std::vector<std::uint8_t> bytes;
    encodeAnt(fant, bytes);
    c.check(bytes.size() == kAntWireSize, "fixed encoded size");
    const std::vector<std::uint8_t> expected{kAntWireVersion,
                                             1,
                                             0x04,
                                             0x03,
                                             0x02,
                                             0x01,
                                             0xFE,
                                             0xFF,
                                             0xFF,
                                             0xFF,
                                             0xD4,
                                             0xC3,
                                             0xB2,
                                             0xA1,
                                             7,
                                             35};
    c.check(bytes == expected, "layout: version, type, then little-endian fields");
    const auto back = decodeAnt(bytes);
    c.check(back && *back == fant, "FANT round-trips");

    const AntMessage bant{AntType::Backward, 9, 1, 0, 255, 0};
    bytes.clear();
    encodeAnt(bant, bytes);
    const auto bback = decodeAnt(bytes);
    c.check(bback && *bback == bant, "BANT round-trips, extremes included");
}

void TestAntDecoderFailsClosed(Checker& c) {
    std::vector<std::uint8_t> good;
    encodeAnt(AntMessage{AntType::Forward, 1, 2, 3, 4, 5}, good);

    auto bad = good;
    bad[0] = kAntWireVersion + 1;
    c.check(!decodeAnt(bad), "another wire version is rejected");
    for (std::uint8_t type : {0, 3, 255}) {
        bad = good;
        bad[1] = type;
        c.check(!decodeAnt(bad), "an unknown ant type is rejected");
    }
    for (std::size_t n = 0; n < good.size(); ++n) {
        const std::vector<std::uint8_t> shortBytes(good.begin(),
                                                   good.begin() + static_cast<long>(n));
        c.check(!decodeAnt(shortBytes), "a truncated ant is rejected");
    }
    bad = good;
    bad.push_back(0);
    c.check(!decodeAnt(bad), "trailing bytes are rejected");
}

void TestAntFuzz(Checker& c) {
    SeededRng rng(20021);
    int accepted = 0;
    for (int i = 0; i < 20000; ++i) {
        std::vector<std::uint8_t> bytes;
        if (i % 2 == 0) {
            // Mutations of a valid ant: flip one to three bytes.
            encodeAnt(AntMessage{AntType::Backward, 11, 22, static_cast<std::uint32_t>(i), 3, 30},
                      bytes);
            const int flips = 1 + rng.uniformInt(3);
            for (int f = 0; f < flips; ++f) {
                bytes[static_cast<std::size_t>(rng.uniformInt(static_cast<int>(bytes.size())))] =
                    static_cast<std::uint8_t>(rng.uniformInt(256));
            }
        } else {
            // Random buffers of 0..24 bytes, the version byte usually right.
            const int n = rng.uniformInt(25);
            for (int b = 0; b < n; ++b)
                bytes.push_back(static_cast<std::uint8_t>(rng.uniformInt(256)));
            if (!bytes.empty() && rng.uniformInt(4) != 0) bytes[0] = kAntWireVersion;
        }
        const auto ant = decodeAnt(bytes);
        if (!ant) continue;
        ++accepted;
        std::vector<std::uint8_t> again;
        encodeAnt(*ant, again);
        if (again != bytes) {
            c.check(false, "an accepted buffer re-encodes to itself");
            return;
        }
    }
    c.check(accepted > 1000, "the fuzz reaches the accepting path");
}

}  // namespace ara::core::test
