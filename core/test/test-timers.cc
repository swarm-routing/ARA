// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Cancellable timers (#99): a discovery's timeout is cancelled when the
// discovery ends first (its BANT arrives, or the router resets), so on a
// scheduler that cancels the stale handler never runs; on one that does not
// (ITimerScheduler's defaults), it runs and does nothing. Either way the
// router sends the same ants.
#include "ara-core-tests.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ara::core::test {

namespace {

/// A chain 1 - 2 - 3; node 1 sends data to 3 once, then the run goes past the
/// discovery timeout.
struct ChainRun {
    std::vector<std::string> trace;
    std::uint64_t ran = 0;
    std::size_t pending = 0;
    bool discovering = true;
};

ChainRun runChain(bool cancels) {
    Network net(7);
    net.events().setCancels(cancels);
    net.addLink(1, 2);
    net.addLink(2, 3);
    (void)net.node(1).onLocalData(3);
    net.events().advance(2.0 * Config{}.discoveryTimeout);
    return {net.trace(), net.events().ran(), net.events().pending(), net.node(1).discovering(3)};
}

}  // namespace

void TestAnsweredDiscoveryCancelsItsTimeout(Checker& c) {
    const ChainRun kept = runChain(false);
    const ChainRun cancelled = runChain(true);
    c.check(!kept.trace.empty() && kept.trace == cancelled.trace,
            "the same ants, cancelled or not");
    c.check(!kept.discovering && !cancelled.discovering, "the BANT ended the discovery");
    c.check(kept.pending == 0 && cancelled.pending == 0, "nothing left to run");
    c.check(cancelled.ran + 1 == kept.ran, "cancelled: the stale timeout never ran");
}

void TestResetCancelsTimeouts(Checker& c) {
    Harness h(5);
    h.events.setCancels(true);
    // A discovery with demand: its timeout retries, and keeps the new handle.
    (void)h.router.onLocalData(9);
    (void)h.router.onLocalData(9);
    c.check(h.out.size() == 1 && h.events.pending() == 1, "one FANT, its timeout pending");
    h.events.advance(Config{}.discoveryTimeout + 0.1);
    c.check(h.out.size() == 2 && h.router.discovering(9) && h.events.pending() == 1,
            "the timeout retried, the retry's timeout pending");
    // A BANT for the retry ends the discovery and cancels that timeout.
    const std::uint32_t retry = h.out.back().decision.message->sequence;
    h.router.onAnt(AntMessage{AntType::Backward, 9, 5, retry, 0, 35}, 2);
    c.check(!h.router.discovering(9) && h.events.pending() == 0, "answered: no timeout pending");

    // A reset cancels every pending timeout.
    (void)h.router.onLocalData(8);
    c.check(h.events.pending() == 1, "a new discovery's timeout pending");
    const std::uint64_t ran = h.events.ran();
    h.router.reset();
    c.check(h.events.pending() == 0, "reset: no timeout pending");
    h.events.advance(4.0 * Config{}.discoveryTimeout);
    c.check(h.events.ran() == ran && h.out.size() == 3, "nothing ran or was sent after the reset");
}

void TestResetDropsPendingRelays(Checker& c) {
    // #287: a FANT relay waits U[0, relayJitterMax); a reset in between (a
    // crash) must not let it out, on a scheduler that cancels or not.
    for (const bool cancels : {false, true}) {
        for (const bool crash : {false, true}) {
            Harness h(5);
            h.events.setCancels(cancels);
            h.router.onAnt(AntMessage{AntType::Forward, 1, 9, 1, 0, 35}, 2);
            const bool pending = h.out.empty() && h.events.pending() == 1;
            if (crash) h.router.reset();
            h.events.advance(Config{}.relayJitterMax + 0.1);
            const std::string kind = std::string(cancels ? "cancelling" : "plain") +
                                     (crash ? ", reset: nothing sent" : ", no reset: relayed");
            c.check(pending && h.out.size() == (crash ? 0u : 1u), kind);
        }
    }
}

}  // namespace ara::core::test
