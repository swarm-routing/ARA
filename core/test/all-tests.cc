// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The list every runner walks.
#include "ara-core-tests.h"

#include <array>

namespace ara::core::test {

namespace {

constexpr std::array<NamedTest, 43> kTests{{
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
    {"buffering (switch): held while the discovery runs, off by default",
     TestBufferHoldsDuringDiscovery},
    {"buffering (switch): the held data goes with a discovery that ends with no trail",
     TestBufferGivesUpWithTheDiscovery},
    {"history: 64-entry cap, oldest evicted first", TestHistoryCap},
    {"history: entries expire 30 s after they are recorded", TestHistoryLifetime},
    {"discovery end to end on a line and a diamond", TestDiscoveryOnALine},
    {"determinism: same seed, same trace", TestDeterminism},
    {"data: Δϕ toward both ends on every hop", TestDataReinforcement},
    {"data: one packet a second keeps both trails alive", TestDataKeepsTrailsAlive},
    {"observer: every callback, from a recording observer", TestObserverCallbacks},
    {"observer: reads only, same decisions and trace with one", TestObserverIsReadOnly},
    {"duplicates: a seen key is a loop; DUPLICATE_ERROR drops that link", TestDuplicateData},
    {"route failure: the failed link is dropped, another trail taken", TestFailureTakesAnotherLink},
    {"route failure: no other trail, back to the previous hop", TestFailureBacktracks},
    {"route failure: at the source, a new discovery", TestFailureAtSourceRediscovers},
    {"route failure: backtracking on a diamond, end to end", TestBacktrackOnADiamond},
    {"statistical selection (switch): each trail's share is ϕ over Σϕ", TestStatisticalShares},
    {"statistical selection (switch): not back to the previous hop",
     TestStatisticalLeavesOutThePreviousHop},
    {"statistical selection (switch): off by default, decisions and draws unchanged",
     TestStatisticalOffIsUnchanged},
    {"statistical selection (switch): same seed, same choices", TestStatisticalDeterminism},
    {"flooded BANT (switch): both relays of a diamond lay the trail", TestFloodedBantOnADiamond},
    {"flooded BANT (switch): jittered broadcast, duplicates, TTL, its ends", TestFloodedBantRelay},
    {"one retry (switch): one other trail per packet at a relay, then back", TestOneRetryAtARelay},
    {"one retry (switch): at the source, and an evicted entry", TestOneRetryAtTheSourceAndEviction},
    {"lifecycle: reset forgets every trail, history and discovery", TestResetForgetsEverything},
    {"lazy decay (switch): every read agrees with the eager sweep", TestLazyDecayAgreesWithEager},
    {"lazy decay (switch): expiry at the threshold", TestLazyDecayExpiresAtTheThreshold},
    {"lazy decay (switch): a full table reclaims unread entries",
     TestLazyDecayReclaimsUnreadEntries},
    {"timers: an answered discovery cancels its timeout", TestAnsweredDiscoveryCancelsItsTimeout},
    {"timers: a reset cancels every pending timeout", TestResetCancelsTimeouts},
    {"timers: a reset drops pending FANT relays", TestResetDropsPendingRelays},
    {"data as FANT (switch): one FANT per packet, no retry FANT", TestDataAsFantAtTheSource},
    {"data as FANT (switch): over the buffer, the cap, and off", TestDataAsFantPrecedenceAndOff},
}};

}  // namespace

std::span<const NamedTest> AllTests() {
    return kTests;
}

}  // namespace ara::core::test
