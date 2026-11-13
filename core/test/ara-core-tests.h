// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// ARA's core test functions. Each records its failures in the Checker; the
// runners (run-core-tests.cc, and the ns-3 suite once ns3/ exists) report them.
#ifndef ARA_CORE_TESTS_H
#define ARA_CORE_TESTS_H

#include "ara-test-support.h"

#include <span>

namespace ara::core::test {

void TestAntRoundtrip(Checker& c);
void TestAntDecoderFailsClosed(Checker& c);
void TestAntFuzz(Checker& c);
void TestFantRelay(Checker& c);
void TestFantDuplicatesAndTtl(Checker& c);
void TestDestinationAnswers(Checker& c);
void TestBantRelay(Checker& c);
void TestEvaporation(Checker& c);
void TestNextHopChoice(Checker& c);
void TestRetryOnlyWithDemand(Checker& c);
void TestRetryLimitAndDoubling(Checker& c);
void TestBufferHoldsDuringDiscovery(Checker& c);
void TestBufferGivesUpWithTheDiscovery(Checker& c);
void TestHistoryCap(Checker& c);
void TestHistoryLifetime(Checker& c);
void TestDiscoveryOnALine(Checker& c);
void TestDeterminism(Checker& c);
void TestDataReinforcement(Checker& c);
void TestDataKeepsTrailsAlive(Checker& c);
void TestObserverCallbacks(Checker& c);
void TestObserverIsReadOnly(Checker& c);
void TestDuplicateData(Checker& c);
void TestFailureTakesAnotherLink(Checker& c);
void TestFailureBacktracks(Checker& c);
void TestFailureAtSourceRediscovers(Checker& c);
void TestBacktrackOnADiamond(Checker& c);

struct NamedTest {
    const char* name;
    void (*run)(Checker&);
};

/// Every core test, in a fixed order.
std::span<const NamedTest> AllTests();

}  // namespace ara::core::test

#endif  // ARA_CORE_TESTS_H
