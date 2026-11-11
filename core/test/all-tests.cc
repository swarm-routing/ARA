// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The list every runner walks.
#include "ara-core-tests.h"

#include <array>

namespace ara::core::test {

namespace {

constexpr std::array<NamedTest, 18> kTests{{
    {"ant codec: roundtrip", TestAntRoundtrip},
    {"ant codec: the decoder fails closed", TestAntDecoderFailsClosed},
    {"ant codec: seeded fuzz", TestAntFuzz},
    {"FANT: trail, reverse pointer, jittered relay", TestFantRelay},
    {"FANT: duplicates and TTL", TestFantDuplicatesAndTtl},
    {"FANT: the destination answers with a BANT", TestDestinationAnswers},
    {"BANT: unicast back along the FANT's path", TestBantRelay},
    {"evaporation: continuous eq. 2 and expiry", TestEvaporation},
    {"next hop: highest pheromone, ties to the lowest address", TestNextHopChoice},
    {"discovery: a retry only while the source keeps sending", TestRetryOnlyWithDemand},
    {"discovery: timeout doubles, at most 2 retries", TestRetryLimitAndDoubling},
    {"history: 64-entry cap, oldest evicted first", TestHistoryCap},
    {"discovery end to end on a line and a diamond", TestDiscoveryOnALine},
    {"determinism: same seed, same trace", TestDeterminism},
    {"data: Δϕ toward both ends on every hop", TestDataReinforcement},
    {"data: one packet a second keeps both trails alive", TestDataKeepsTrailsAlive},
    {"observer: every callback, from a recording observer", TestObserverCallbacks},
    {"observer: reads only, same decisions and trace with one", TestObserverIsReadOnly},
}};

}  // namespace

std::span<const NamedTest> AllTests() {
    return kTests;
}

}  // namespace ara::core::test
